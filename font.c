#include "font.h"

#include <gdk/gdk.h>

#include "global.h"
#include "cfg.h"
#include FT_ERRORS_H

FT_Library library;
teddy_fontset_t variable_main_fonts;
teddy_fontset_t monospace_main_fonts;
teddy_font_t posbox_font;

const char graphics[] = "STARTFONT 2.1\n\
COMMENT Graphics font for directory trees\n\
FONT teddygraph.16.font\n\
SIZE 16 75 75\n\
FONTBOUNDINGBOX 14 14 0 0\n\
STARTPROPERTIES 7\n\
PIXEL_SIZE 16\n\
POINT_SIZE 160\n\
FONT_ASCENT 14\n\
FONT_DESCENT 0\n\
CHARSET_REGISTRY \"iso10646\"\n\
CHARSET_ENCODING \"1\"\n\
DEFAULT_CHAR 0\n\
ENDPROPERTIES\n\
CHARS 2\n\
STARTCHAR U+E650\n\
ENCODING 58960\n\
SWIDTH 812 0\n\
DWIDTH 13 0\n\
BBX 13 14 0 0\n\
BITMAP\n\
0000\n\
0000\n\
3fe0\n\
2020\n\
2220\n\
2220\n\
2fa0\n\
2220\n\
2220\n\
2020\n\
3fe0\n\
0000\n\
0000\n\
0000\n\
ENDCHAR\n\
STARTCHAR U+E651\n\
ENCODING 58961\n\
SWIDTH 812 0\n\
DWIDTH 13 0\n\
BBX 13 14 0 0\n\
BITMAP\n\
0000\n\
0000\n\
3fe0\n\
2020\n\
2020\n\
2020\n\
2fa0\n\
2020\n\
2020\n\
2020\n\
3fe0\n\
0000\n\
0000\n\
0000\n\
ENDCHAR\n\
STARTCHAR U+E652\n\
ENCODING 58962\n\
SWIDTH 812 0\n\
DWIDTH 13 0\n\
BBX 13 14 0 0\n\
BITMAP\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
0000\n\
ENDCHAR\n\
ENDFONT\n";

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

static void fontconfig_init_from_pattern(FcPattern *match, teddy_font_t *font) {
	FcChar8 *fontfile;
	if (FcPatternGetString(match, FC_FILE, 0, &fontfile) != FcResultMatch) {
		printf("Font file not found\n");
		exit(EXIT_FAILURE);
	}

	double size;
	if (FcPatternGetDouble(match, FC_SIZE, 0, &size) != FcResultMatch) {
		size = 12.0;
	}

	int index;
	if (FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch) {
		index = 0;
	}

	teddy_font_init_ex(font, (const char *)fontfile, size, index);
}

static void teddy_font_init_fontconfig_pattern(teddy_font_t *font, const char *fontpattern) {
	FcResult result;

	FcPattern *pat = FcNameParse((const FcChar8 *)fontpattern);
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcPattern *match = FcFontMatch(NULL, pat, &result);

	if (!match) {
		printf("Couldn't resolve font pattern [%s], this shouldn't happen\n", fontpattern);
		FcPatternDestroy(pat);
		exit(EXIT_FAILURE);
	}

	fontconfig_init_from_pattern(match, font);

	FcPatternDestroy(match);
	FcPatternDestroy(pat);
}

static void teddy_font_init_direct_path(teddy_font_t *font, const char *fontspec) {
	char *saveptr;
	char *fontspec_copy = strdup(fontspec);
	char *fontpath = strtok_r(fontspec_copy, ":", &saveptr);

	if (fontpath == NULL) {
		fprintf(stderr, "Bizzarre error interpreting the font specification\n");
		exit(EXIT_FAILURE);
	}

	char *sizestr = strtok_r(NULL, ":", &saveptr);
	double size = atof(sizestr);

	teddy_font_init_ex(font, fontpath, size, 0);

	free(fontspec_copy);
}

static void teddy_font_init(teddy_font_t *font, const char *fontpattern) {
	if (fontpattern[0] == '/') {
		teddy_font_init_direct_path(font, fontpattern);
	} else {
		teddy_font_init_fontconfig_pattern(font, fontpattern);
	}
}

static void teddy_font_free(teddy_font_t *font) {
	cairo_scaled_font_destroy(font->cairofont);
	cairo_font_options_destroy(font->font_options);
	cairo_font_face_destroy(font->cairoface);

	FT_Done_Face(font->face);
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

static void fontset_init(const char *fontfile, teddy_fontset_t *fontset) {
	fontset->count = 0;

	for (int i = 0; i <= 0xffff; ++i) {
		fontset->map[i] = 0xff;
	}

	FcResult result;

	FcPattern *pat = FcNameParse((const FcChar8 *)fontfile);
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcFontSet *set = FcFontSort(NULL, pat, FcTrue, NULL, &result);

	if (!set) {
		printf("Couldn't resolve font pattern [%s], this shouldn't happen\n", fontfile);
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

			fontconfig_init_from_pattern(match, fontset->fonts+fontset->count);
			++(fontset->count);

			FcCharSetMerge(accumulator, cset, NULL);
			acc_count += c;

			if (fontset->count >= 0xfe) break;
		}
	}

	teddy_font_t *graphics_font = fontset->fonts+fontset->count;
	FT_Open_Args graphics_open_args = { FT_OPEN_MEMORY, graphics, sizeof(graphics), NULL, NULL, 0, 0, NULL };
	int error = FT_Open_Face(library, &graphics_open_args, 0, &(graphics_font->face));
	if (error) {
		printf("Error loading builtin font\n");
		exit(EXIT_FAILURE);
	}
	teddy_finish_init_font(graphics_font, 16, false);

	fontset->map[0xE650] = fontset->count;
	fontset->map[0xE651] = fontset->count;
	fontset->map[0xE652] = fontset->count;
	++(fontset->count);
}

static void fontset_free(teddy_fontset_t *fontset) {
	for (int i = 0; i < fontset->count; ++i) {
		teddy_font_free(fontset->fonts + i);
	}
}

void teddy_font_real_init(void) {
	int error = FT_Init_FreeType(&library);
	if (error) {
		printf("Freetype initialization error\n");
		exit(EXIT_FAILURE);
	}

	teddy_font_init(&posbox_font, config[CFG_POSBOX_FONT].strval);
	fontset_init(config[CFG_MAIN_FONT].strval, &variable_main_fonts);
	fontset_init(config[CFG_MAIN_MONOSPACE_FONT].strval, &monospace_main_fonts);
}


void teddy_font_real_free(void) {
	teddy_font_free(&posbox_font);
	fontset_free(&variable_main_fonts);
	fontset_free(&monospace_main_fonts);
}

