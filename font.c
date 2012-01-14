#include "font.h"

#include <gdk/gdk.h>

#include "cfg.h"

FT_Library library;
teddy_font_t main_font;
teddy_font_t posbox_font;

static void teddy_font_init_ex(teddy_font_t *font,  const char *fontfile, double size, int face_index) {
	gdouble dpi = gdk_screen_get_resolution(gdk_screen_get_default());
	double text_size = dpi / 72.0 * size;

	int error = FT_New_Face(library, (const char *)fontfile, face_index, &(font->face));
	if (error) {
		printf("Error loading freetype font\n");
		exit(EXIT_FAILURE);
	}

	error = FT_Set_Char_Size(font->face, 0, size * 64, dpi, dpi);
	if (error) {
		printf("Error loading freetype font\n");
		exit(EXIT_FAILURE);
	}

	font->cairoface = cairo_ft_font_face_create_for_ft_face(font->face, FT_LOAD_FORCE_AUTOHINT);

	cairo_matrix_init(&(font->font_size_matrix), text_size, 0, 0, text_size, 0, 0);
	cairo_matrix_init(&(font->font_ctm), 1, 0, 0, 1, 0, 0);
	font->font_options = cairo_font_options_create();

	cairo_font_options_set_antialias(font->font_options, CAIRO_ANTIALIAS_GRAY);
	cairo_font_options_set_hint_style(font->font_options, CAIRO_HINT_STYLE_SLIGHT);

	font->cairofont = cairo_scaled_font_create(font->cairoface, &(font->font_size_matrix), &(font->font_ctm), font->font_options);
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

	FcChar8 *fontfile;
	if (FcPatternGetString(match, FC_FILE, 0, &fontfile) != FcResultMatch) {
		printf("Font file not found [%s]\n", fontpattern);
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

void teddy_font_real_init(void) {
	int error = FT_Init_FreeType(&library);
	if (error) {
		printf("Freetype initialization error\n");
		exit(EXIT_FAILURE);
	}

	teddy_font_init(&main_font, config[CFG_MAIN_FONT].strval);
	teddy_font_init(&posbox_font, config[CFG_POSBOX_FONT].strval);
}

void teddy_font_real_free(void) {
	teddy_font_free(&main_font);
	teddy_font_free(&posbox_font);
}
