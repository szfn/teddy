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
    int lineno; // real line number
    double start_y;
    double end_y;
    struct _real_line_t *prev;
    struct _real_line_t *next;
} real_line_t;

typedef struct _buffer_t {
    char *name;
    int has_filename;
    
    /* Font face stuff */
    FT_Library *library;
    acmacs_font_t main_font;
    acmacs_font_t posbox_font;

    /* Font secondary metrics of main font */
    double em_advance;
    double space_advance;
    double ex_height;
    double line_height;
    double ascent, descent;

    /* Buffer's text and glyphs */
    real_line_t *real_line;

    /* Buffer's secondary properties (calculated) */
    double rendered_height;
    double rendered_width;

    /* Cursor */
    real_line_t *cursor_line;
    int cursor_glyph;

    /* Mark */
    int mark_glyph;
    int mark_lineno;

    /* User options */
    int tab_width;
    double left_margin;
    double right_margin;
} buffer_t;

buffer_t *buffer_create(FT_Library *library);
void load_text_file(buffer_t *buffer, const char *filename);
void save_to_text_file(buffer_t *buffer);
void buffer_free(buffer_t *buffer);

void buffer_line_adjust_glyphs(buffer_t *buffer, real_line_t *line, double x, double y, double window_width, double window_height, double *y_increment, double *line_end_width);

int buffer_line_insert_utf8_text(buffer_t *buffer, real_line_t *line, char *text, int len, int insertion_point);
void buffer_line_remove_glyph(buffer_t *buffer, real_line_t *line, int glyph_index);

void buffer_reflow_softwrap(buffer_t *buffer, double softwrap_width);
int buffer_reflow_softwrap_real_line(buffer_t *buffer, real_line_t *line, int cursor_increment, int ignore_cursor);

void buffer_real_cursor(buffer_t *buffer, real_line_t **real_line, int *real_glyph);
void buffer_set_to_real(buffer_t *buffer, real_line_t *real_line, int real_glyph);
void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y);
void buffer_cursor_position(buffer_t *buffer, double *x, double *y);
void buffer_cursor_line_rectangle(buffer_t *buffer, double origin_x, double origin_y, double *x, double *y, double *height, double *width);

real_line_t *buffer_copy_line(buffer_t *buffer, real_line_t *real_line, int start, int size);
void buffer_line_delete_from(buffer_t *buffer, real_line_t *real_line, int start, int size);
void buffer_real_line_insert(buffer_t *buffer, real_line_t *insertion_line, real_line_t* real_line);

real_line_t *buffer_line_by_number(buffer_t *buffer, int lineno);

void buffer_get_selection(buffer_t *buffer, real_line_t **start_line, int *start_glyph, real_line_t **end_line, int *end_glyph);

void buffer_join_lines(buffer_t *buffer, real_line_t *line1, real_line_t *line2);

void line_get_glyph_coordinates(buffer_t *buffer, real_line_t *line, int glyph, double *x, double *y);

char *buffer_lines_to_text(buffer_t *buffer, real_line_t *start_line, real_line_t *end_line, int start_glyph, int end_glyph);

void buffer_split_line(buffer_t *buffer, real_line_t *line, int glyph);
void buffer_insert_multiline_text(buffer_t *buffer, real_line_t *line, int glyph, char *text);

void debug_print_lines_state(buffer_t *buffer);
void debug_print_real_lines_state(buffer_t *buffer);

#endif
