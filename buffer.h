#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>

#include "font.h"

typedef struct _my_glyph_info_t {
    double kerning_correction;
    double x_advance;
    uint32_t code;
} my_glyph_info_t;

typedef struct _real_line_t {
    cairo_glyph_t *glyphs;
    my_glyph_info_t *glyph_info;
    int allocated;
    int cap;
    struct _real_line_t *next;
} real_line_t;

typedef struct _display_line_t {
    real_line_t *real_line;
    int offset;
    int size;
    int hard_end;
    struct _display_line_t *next;
} display_line_t;

typedef struct _buffer_t {
    /* Font face stuff */
    FT_Library *library;
    acmacs_font_t main_font;
    acmacs_font_t posbox_font;

    /* Font secondary metrics of main font */
    double em_advance;
    double ex_height;
    double line_height;
    double ascent, descent;

    /* Buffer's text and glyphs */
    real_line_t *real_line;
    display_line_t *display_line;
    int display_lines_count;

    /* Buffer's secondary properties (calculated) */
    double rendered_height;
    double rendered_width;

    /* Cursor */
    int cursor_line, cursor_glyph;

    /* User options */
    int tab_width;
    double left_margin;
    double right_margin;
} buffer_t;

buffer_t *buffer_create(FT_Library *library);
void buffer_free(buffer_t *buffer);
void load_text_file(buffer_t *buffer, const char *filename);
double buffer_line_adjust_glyphs(buffer_t *buffer, display_line_t *display_line, double x, double y);
void buffer_cursor_position(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y);
void buffer_cursor_line_rectangle(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y, double *height, double *width);
void buffer_move_cursor_to_position(buffer_t *buffer, double origin_x, double origin_y, double x, double y);
void buffer_line_insert_utf8_text(buffer_t *buffer, real_line_t *line, char *text, int len, int insertion_point, int move_cursor);
void buffer_reflow_softwrap(buffer_t *buffer, double softwrap_width);

#endif
