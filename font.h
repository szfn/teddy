#ifndef __TEDDY_FONT_H__
#define __TEDDY_FONT_H__

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct _teddy_font_t {
    FT_Face face;
    cairo_font_face_t *cairoface;
    cairo_scaled_font_t *cairofont;
    cairo_matrix_t font_size_matrix, font_ctm;
    cairo_font_options_t *font_options;
} teddy_font_t;

void teddy_font_init(teddy_font_t *font, FT_Library *library, const char *fontpattern);
void teddy_font_free(teddy_font_t *font);

#endif
