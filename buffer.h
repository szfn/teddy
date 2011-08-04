#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct _line_t {
    char *text;
    int allocated_text;
    int text_cap;
} line_t;

typedef struct _buffer_t {
    /* Font face stuff */
    FT_Library *library;
    FT_Face face;
    cairo_font_face_t *cairoface;
    cairo_scaled_font_t *cairofont;
    cairo_matrix_t font_size_matrix, font_ctm;
    cairo_font_options_t *font_options;

    /* Buffer's text and glyphs */
    line_t *lines;
    int allocated_lines;
    int lines_cap;

    /* User options */
    int tab_width;
    
} buffer_t;

buffer_t *buffer_create(FT_Library *library);
void load_text_file(buffer_t *buffer, const char *filename);


#endif
