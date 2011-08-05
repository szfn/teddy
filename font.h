#ifndef __ACMACS_FONT_H__
#define __ACMACS_FONT_H__

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct _acmacs_font_t {
    FT_Face face;
    cairo_font_face_t *cairoface;
    cairo_scaled_font_t *cairofont;
    cairo_matrix_t font_size_matrix, font_ctm;
    cairo_font_options_t *font_options;
} acmacs_font_t;

void acmacs_font_init(acmacs_font_t *font, FT_Library *library);
void acmacs_font_free(acmacs_font_t *font);

#endif
