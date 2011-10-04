#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>

#include "font.h"
#include "undo.h"
#include "jobs.h"
#include "point.h"

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
	double y_increment;
	struct _real_line_t *prev;
	struct _real_line_t *next;
} real_line_t;

typedef struct _buffer_t {
	char *name;
	char *path;
	char *wd;
	int has_filename;
	int modified;
	int editable;

	job_t *job;
	
	/* Font face stuff */
	FT_Library *library;
	teddy_font_t main_font;
	teddy_font_t posbox_font;

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

	/* Cursor and mark*/
	lpoint_t cursor;
	lpoint_t mark;

	/* Undo information */
	undo_t undo;

	/* User options */
	int tab_width;
	double left_margin;
	double right_margin;
} buffer_t;

// utility function to convert first codepoint in utf8 stream into an utf32 codepoint
uint32_t utf8_to_utf32(const char *text, int *src, int len);


buffer_t *buffer_create(FT_Library *library);
void buffer_free(buffer_t *buffer);

/* Initialization completion function (at least one of these two must be called after buffer_create, before the buffer is used in any way)
   - load_text_file: loads a text file
   - load_empyt: finalize the buffer initialization as empty
 */
int load_text_file(buffer_t *buffer, const char *filename);
void load_empty(buffer_t *buffer);

// save the buffer to its file (if exists, otherwise fails)
void save_to_text_file(buffer_t *buffer);

/*
  Sets working directory for the buffer (only works if there is no associated file
 */
void buffer_cd(buffer_t *buffer, const char *wd);

/*
  Mark management
  buffer_set_mark_at_cursor: copies cursor point into mark point
  buffer_unset_mark: sets mark to null
 */
void buffer_set_mark_at_cursor(buffer_t *buffer);
void buffer_unset_mark(buffer_t *buffer);

// replace current selection with new_text (main editing function)
void buffer_replace_selection(buffer_t *buffer, const char *new_text);

// undo
void buffer_undo(buffer_t *buffer);

// returns current selection
void buffer_get_selection(buffer_t *buffer, lpoint_t *start, lpoint_t *end);

// returns pointer to mark and cursor for current selection (or NULL if no selection exists)
void buffer_get_selection_pointers(buffer_t *buffer, lpoint_t **start, lpoint_t **end);

// converts a selection of line from this buffer into text
char *buffer_lines_to_text(buffer_t *buffer, lpoint_t *start, lpoint_t *end);

// converts a line from this buffer into text
char *buffer_line_to_text(buffer_t *buffer, real_line_t *line);

// moves cursor by one glyph
void buffer_move_cursor(buffer_t *buffer, int direction);

// sets character positions if width has changed
void buffer_typeset_maybe(buffer_t *buffer, double width);

// functions to get screen coordinates of things (yes, I have no idea anymore what the hell they do or are used for)
void buffer_cursor_position(buffer_t *buffer, double *x, double *y);
void line_get_glyph_coordinates(buffer_t *buffer, lpoint_t *point, double *x, double *y);
void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y);

int buffer_real_line_count(buffer_t *buffer);

// removes trailing spaces from line unless line is exclusively made out of spaces
void buffer_line_clean_trailing_spaces(buffer_t *buffer, real_line_t *line);

void freeze_selection(buffer_t *buffer, selection_t *selection, lpoint_t *start, lpoint_t *end);
void buffer_thaw_selection(buffer_t *buffer, selection_t *selection, lpoint_t *start, lpoint_t *end);

#endif
