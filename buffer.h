#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>

#include "undo.h"
#include "jobs.h"
#include "point.h"
#include "parmatch.h"
#include "critbit.h"
#include "cfg.h"

typedef struct _my_glyph_info_t {
	double kerning_correction;
	double x_advance;
	uint32_t code;
	uint8_t color;

	unsigned long glyph_index;
	uint8_t fontidx;
	double x;
	double y;
} my_glyph_info_t;

typedef struct _real_line_t {
	my_glyph_info_t *glyph_info;
	int allocated;
	int cap;
	int lineno; // real line number
	double start_y;
	double end_y;
	double y_increment;
	uint8_t lexy_state_start, lexy_state_end;
	struct _real_line_t *prev;
	struct _real_line_t *next;
} real_line_t;

enum select_type { BST_NORMAL = 0, BST_WORDS, BST_LINES };

enum tab_mode {
	TAB_FIXED = 0,
	TAB_MODN = 1,
};

typedef struct _buffer_t {
	char *path;
	int has_filename;
	int modified;
	int editable;
	uint8_t default_color;
	int inotify_wd;
	time_t mtime;
	bool stale;

	job_t *job;

	config_t config;

	/* Font secondary metrics of main font */
	double em_advance;
	double space_advance;
	double ex_height;
	double line_height;
	double ascent, descent;
	double underline_position, underline_thickness;

	/* Buffer's text and glyphs */
	real_line_t *real_line;

	/* Buffer's secondary properties (calculated) */
	double rendered_height;
	double rendered_width;

	/* Cursor and mark*/
	lpoint_t cursor;
	lpoint_t mark;
	lpoint_t savedmark;
	enum select_type select_type;
	parmatch_t parmatch;

	/* Undo information */
	undo_t undo;

	/* User options */
	double left_margin;
	double right_margin;

	/* Lexy stuff */
	real_line_t *lexy_last_update_line;

	/* Scripting support */
	GHashTable *props;
	char *keyprocessor;
	void (*onchange)(struct _buffer_t *buffer);


	/* autocompletion */
	critbit0_tree cbt;
} buffer_t;

enum movement_type_t {
	MT_ABS = 0, // move to absolute line/column
	MT_REL, // relative move
	MT_END, // move to end
	MT_START, // move to first non-whitespace character (buffer_move_point_glyph only)
	MT_HOME, // toggle between first column and first non-whitespace character (buffer_move_point_glyph only)
	MT_RELW, // word based relative move (buffer_move_point_glyph only)
};

buffer_t *buffer_create(void);
void buffer_free(buffer_t *buffer, bool save_critbit);

/* Initialization completion function (at least one of these two must be called after buffer_create, before the buffer is used in any way)
   - load_text_file: loads a text file
   - load_dir: loads a directory
   - load_empyt: finalize the buffer initialization as empty
 */
int load_text_file(buffer_t *buffer, const char *filename);
void load_empty(buffer_t *buffer);
int load_dir(buffer_t *buffer, const char *dirname);

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
  buffer_change_select_type: change selection type
 */
void buffer_set_mark_at_cursor(buffer_t *buffer);
void buffer_unset_mark(buffer_t *buffer);
void buffer_change_select_type(buffer_t *buffer, enum select_type select_type);
void buffer_extend_selection_by_select_type(buffer_t *buffer);
void buffer_update_parmatch(buffer_t *buffer);

// replace current selection with new_text (main editing function)
void buffer_replace_selection(buffer_t *buffer, const char *new_text);
void buffer_replace_region(buffer_t *buffer, const char *new_text, lpoint_t *start, lpoint_t *end);

// undo
void buffer_undo(buffer_t *buffer, bool redo);

// returns current selection
void buffer_get_selection(buffer_t *buffer, lpoint_t *start, lpoint_t *end);

// returns pointer to mark and cursor for current selection (or NULL if no selection exists)
void buffer_get_selection_pointers(buffer_t *buffer, lpoint_t **start, lpoint_t **end);

// converts a selection of line from this buffer into text
char *buffer_lines_to_text(buffer_t *buffer, lpoint_t *start, lpoint_t *end);

// sets character positions if width has changed
void buffer_typeset_maybe(buffer_t *buffer, double width, bool single_line, bool force);

// functions to get screen coordinates of things (yes, I have no idea anymore what the hell they do or are used for)
void line_get_glyph_coordinates(buffer_t *buffer, lpoint_t *point, double *x, double *y);
void buffer_point_from_position(buffer_t *buffer, double x, double y, lpoint_t *p);
void buffer_move_cursor_to_position(buffer_t *buffer, double x, double y);

void buffer_config_changed(buffer_t *buffer);

void buffer_set_onchange(buffer_t *buffer, void (*fn)(buffer_t *buffer));

bool buffer_move_point_line(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg);
bool buffer_move_point_glyph(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg);

/* writes in r the indent of cursor_line + a newline and the 0 byte */
void buffer_indent_newline(buffer_t *buffer, char *r);

/* internal word autocompletion functions */
void buffer_wordcompl_init_charset(void);
uint16_t *buffer_wordcompl_word_at_cursor(buffer_t *buffer, size_t *prefix_len);
void buffer_wordcompl_update(buffer_t *buffer, critbit0_tree *cbt);
uint16_t *buffer_cmdcompl_word_at_cursor(buffer_t *buffer, size_t *prefix_len);
uint16_t *buffer_historycompl_word_at_cursor(buffer_t *buffer, size_t *prefix_len);

// removes all text from a buffer
void buffer_aux_clear(buffer_t *buffer);

void buffer_get_extremes(buffer_t *buffer, lpoint_t *start, lpoint_t *end);
char *buffer_all_lines_to_text(buffer_t *buffer);
void buffer_select_all(buffer_t *buffer);
void buffer_wordcompl_update_line(real_line_t *line, critbit0_tree *c);
char *buffer_get_selection_text(buffer_t *buffer);

#endif
