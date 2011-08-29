#include "font.h"

void acmacs_font_init(acmacs_font_t *font, FT_Library *library, const char *fontpattern) {
    int error;
    FcPattern *pat, *match;
    FcResult result;
    FcChar8 *fontfile;
    double size;
    double text_size;

    pat = FcNameParse((const FcChar8 *)fontpattern);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    match = FcFontMatch(NULL, pat, &result);

    if (!match) {
	printf("Couldn't resolve font pattern [%s], this shouldn't happen\n", fontpattern);
	exit(EXIT_FAILURE);
    }

    if (FcPatternGetString(match, FC_FILE, 0, &fontfile) != FcResultMatch) {
	printf("Font file not found [%s]\n", fontpattern);
	exit(EXIT_FAILURE);
    }

    if (FcPatternGetDouble(match, FC_SIZE, 0, &size) != FcResultMatch) {
	size = 12.0;
    }

    text_size = 96.0 / 72.0 * size;
    
    error = FT_New_Face(*library, (const char *)fontfile, 0, &(font->face));
    if (error) {
        printf("Error loading freetype font\n");
        exit(EXIT_FAILURE);
    }

    font->cairoface = cairo_ft_font_face_create_for_ft_face(font->face, 0);

    cairo_matrix_init(&(font->font_size_matrix), text_size, 0, 0, text_size, 0, 0);
    cairo_matrix_init(&(font->font_ctm), 1, 0, 0, 1, 0, 0);
    font->font_options = cairo_font_options_create();

    font->cairofont = cairo_scaled_font_create(font->cairoface, &(font->font_size_matrix), &(font->font_ctm), font->font_options);
}

void acmacs_font_free(acmacs_font_t *font) {
    cairo_scaled_font_destroy(font->cairofont);
    cairo_font_options_destroy(font->font_options);
    cairo_font_face_destroy(font->cairoface);

    FT_Done_Face(font->face);
}
