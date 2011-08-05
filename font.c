#include "font.h"

void acmacs_font_init(acmacs_font_t *font, FT_Library *library, const char *fontfile, int text_size) {
    int error;
    
    error = FT_New_Face(*library, fontfile, 0, &(font->face));
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
