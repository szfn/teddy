#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct _my_glyph_info_t {
    double kerning_correction;
    double x_advance;
} my_glyph_info_t;

typedef struct _line_t {
    char *text;
    int allocated_text;
    int text_cap;

    cairo_glyph_t *glyphs;
    my_glyph_info_t *glyph_info;
    int allocated_glyphs;
    int glyphs_cap;
} line_t;

typedef struct _buffer_t {
    /* Font face stuff */
    FT_Library *library;
    FT_Face face;
    cairo_font_face_t *cairoface;
    cairo_scaled_font_t *cairofont;
    cairo_matrix_t font_size_matrix, font_ctm;
    cairo_font_options_t *font_options;

    /* Font secondary metrics */
    double em_advance;
    double line_height;
    double ascent, descent;

    /* Buffer's text and glyphs */
    line_t *lines;
    int allocated_lines;
    int lines_cap;

    /* Buffer's secondary properties (calculated) */
    double rendered_height;
    double rendered_width;

    /* Cursor */
    int cursor_line, cursor_glyph;

    /* User options */
    int tab_width;
    double left_margin;
} buffer_t;

buffer_t *buffer_create(FT_Library *library);
void buffer_free(buffer_t *buffer);
void load_text_file(buffer_t *buffer, const char *filename);
void buffer_line_adjust_glyphs(buffer_t *buffer, int line_idx, double x, double y);
void buffer_cursor_position(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y);
void buffer_cursor_line_rectangle(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y, double *height, double *width);


#endif
