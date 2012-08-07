#include "foundry.h"

#include <cairo-ft.h>
#include <glib.h>

#include "global.h"

FT_Library library;

typedef struct _teddy_font_t {
	FT_Face face;
	cairo_font_face_t *cairoface;
	cairo_scaled_font_t *cairofont;
	cairo_matrix_t font_size_matrix, font_ctm;
	cairo_font_options_t *font_options;
	FT_Face scaled_face;
} teddy_font_t;

typedef struct _teddy_fontset_t {
	teddy_font_t fonts[0xff];
	uint8_t map[0xfffff+1];
	int count;
} teddy_fontset_t;

GHashTable *fontset_table;

static void fontset_free(teddy_fontset_t *fontset) {
	for (int i = 0; i < fontset->count; ++i) {
		cairo_font_face_destroy(fontset->fonts[i].cairoface);
		cairo_scaled_font_destroy(fontset->fonts[i].cairofont);
		cairo_font_options_destroy(fontset->fonts[i].font_options);
		FT_Done_Face(fontset->fonts[i].face);
	}
	free(fontset);
}

void foundry_init(void) {
	FcInit();
	int error = FT_Init_FreeType(&library);
	if (error) {
		printf("Freetype initialization error\n");
		exit(EXIT_FAILURE);
	}

	fontset_table = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)fontset_free);
}

static void teddy_finish_init_font(teddy_font_t *font, double size, bool set_ft_size) {
	gdouble dpi = gdk_screen_get_resolution(gdk_screen_get_default());
	double text_size = dpi / 72.0 * size;

	if (set_ft_size) {
		int error = FT_Set_Char_Size(font->face, 0, size * 64, dpi, dpi);
		if (error) {
			printf("Error loading freetype font %02X\n", error);
			exit(EXIT_FAILURE);
		}
	}

	font->cairoface = cairo_ft_font_face_create_for_ft_face(font->face, FT_LOAD_FORCE_AUTOHINT);

	cairo_matrix_init(&(font->font_size_matrix), text_size, 0, 0, text_size, 0, 0);
	cairo_matrix_init(&(font->font_ctm), 1, 0, 0, 1, 0, 0);
	font->font_options = cairo_font_options_create();

	cairo_font_options_set_antialias(font->font_options, CAIRO_ANTIALIAS_GRAY);
	cairo_font_options_set_hint_style(font->font_options, CAIRO_HINT_STYLE_SLIGHT);

	font->cairofont = cairo_scaled_font_create(font->cairoface, &(font->font_size_matrix), &(font->font_ctm), font->font_options);
}

static void teddy_font_init_ex(teddy_font_t *font,  const char *fontfile, double size, int face_index) {
	int error = FT_New_Face(library, (const char *)fontfile, face_index, &(font->face));
	if (error) {
		printf("Error loading freetype font\n");
		exit(EXIT_FAILURE);
	}

	teddy_finish_init_font(font, size, true);
}

static void fontconfig_init_from_pattern(FcPattern *match, teddy_font_t *font, double default_size) {
	FcChar8 *fontfile;
	if (FcPatternGetString(match, FC_FILE, 0, &fontfile) != FcResultMatch) {
		printf("Font file not found\n");
		exit(EXIT_FAILURE);
	}

	double size;
	if (FcPatternGetDouble(match, FC_SIZE, 0, &size) != FcResultMatch) {
		size = default_size;
	}

	int index;
	if (FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch) {
		index = 0;
	}

	teddy_font_init_ex(font, (const char *)fontfile, size, index);
}

static void match_write_coverage(teddy_fontset_t *fontset, int idx, FcCharSet *cset, FcCharSet *accumulator) {
	FcChar32 ucs4;
	FcChar32 map[FC_CHARSET_MAP_SIZE];
	FcChar32 next;

	FcCharSet *charnews = FcCharSetSubtract(cset, accumulator);

	for (ucs4 = FcCharSetFirstPage(charnews, map, &next); ucs4 != FC_CHARSET_DONE; ucs4 = FcCharSetNextPage(charnews, map, &next)) {
		for (int i = 0; i < FC_CHARSET_MAP_SIZE; i++) {
			if (!map[i]) continue;
			for (int j = 0; j < 32; j++) {
				if (!(map[i] & (1 << j))) continue;
				uint32_t cur = ucs4 + i * 32 + j;
				if (cur > 0xffff) continue;
				fontset->map[cur] = idx;
			}
		}
	}
}

static teddy_fontset_t *fontset_new(const char *fontname) {
	teddy_fontset_t *fontset = malloc(sizeof(teddy_fontset_t));
	alloc_assert(fontset);

	fontset->count = 0;

	for (int i = 0; i <= 0xffff; ++i) {
		fontset->map[i] = 0xff;
	}

	FcResult result;

	FcPattern *pat = FcNameParse((const FcChar8 *)fontname);
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	double size;
	if (FcPatternGetDouble(pat, FC_SIZE, 0, &size) != FcResultMatch) {
		size = 12.0;
	}

	FcFontSet *set = FcFontSort(NULL, pat, FcTrue, NULL, &result);

	if (!set) {
		printf("Couldn't resolve font pattern [%s], this shouldn't happen\n", fontname);
		FcPatternDestroy(pat);
		exit(EXIT_FAILURE);
	}

	FcCharSet *accumulator = FcCharSetCreate();
	alloc_assert(accumulator);

	int acc_count = 0;

	for (int i = 0; i < set->nfont; ++i) {
		FcPattern *match = set->fonts[i];

		FcCharSet *cset;

		if (FcPatternGetCharSet(match, FC_CHARSET, 0, &cset) != FcResultMatch) continue;

		FcChar32 c = FcCharSetSubtractCount(cset, accumulator);

		int limit = 0;
		if (acc_count > 0) {
			limit = 128;
		}
		if (acc_count > 1024) {
			limit = 512;
		}

		if (c > limit) {
			match_write_coverage(fontset, fontset->count, cset, accumulator);

			fontconfig_init_from_pattern(match, fontset->fonts+fontset->count, size);
			++(fontset->count);

			FcCharSetMerge(accumulator, cset, NULL);
			acc_count += c;

			if (fontset->count >= 0xfe) break;
		}
	}

	return fontset;
}

teddy_fontset_t *foundry_lookup(const char *name, bool lock) {
	teddy_fontset_t *r = g_hash_table_lookup(fontset_table, name);
	if (!r) {
		r = fontset_new(name);
		g_hash_table_insert(fontset_table, (char *)name, r);
	}

	if (lock) {
		for (int i = 0; i < r->count; ++i) {
			r->fonts[i].scaled_face = cairo_ft_scaled_font_lock_face(r->fonts[i].cairofont);
		}
	}

	return r;
}

int fontset_fontidx(teddy_fontset_t *fontset, uint32_t code) {
	uint8_t fontidx = (code <= 0xffff) ? fontset->map[(uint16_t)code] : 0;
	if (fontidx >= fontset->count) fontidx = 0;
	return fontidx;
}

FT_UInt fontset_glyph_index(teddy_fontset_t *fontset, int fondidx, uint32_t code) {
	return FT_Get_Char_Index(fontset->fonts[fondidx].scaled_face, code);
}

double fontset_get_kerning(teddy_fontset_t *fontset, int fontidx, FT_UInt previous_glyph, FT_UInt glyph) {
	if (FT_HAS_KERNING(fontset->fonts[fontidx].scaled_face) && previous_glyph && glyph) {
		FT_Vector delta;

		FT_Get_Kerning(fontset->fonts[fontidx].scaled_face, previous_glyph, glyph, FT_KERNING_DEFAULT, &delta);

		return delta.x >> 6;
	} else {
		return 0.0;
	}
}

double fontset_x_advance(teddy_fontset_t *fontset, int fontidx, FT_UInt glyph) {
	cairo_text_extents_t extents;
	cairo_glyph_t g;
	g.index = glyph;
	g.x = 0.0;
	g.y = 0.0;

	cairo_scaled_font_glyph_extents(fontset->fonts[fontidx].cairofont, &g, 1, &extents);

	return extents.x_advance;
}

void foundry_release(teddy_fontset_t *fontset) {
	for (int i = 0; i < fontset->count; ++i) {
		cairo_ft_scaled_font_unlock_face(fontset->fonts[i].cairofont);
	}
}

cairo_scaled_font_t *fontset_get_cairofont(teddy_fontset_t *fontset, int fontidx) {
	return fontset->fonts[0].cairofont;
}

cairo_scaled_font_t *fontset_get_cairofont_by_name(const char *name, int fontidx) {
	teddy_fontset_t *fontset = foundry_lookup(name, false);
	return fontset_get_cairofont(fontset, fontidx);
}

void foundry_free(void) {
	g_hash_table_destroy(fontset_table);
	FT_Done_FreeType(library);
	//FcFini();
}
