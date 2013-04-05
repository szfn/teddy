#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "jobs.h"
#include "cfg.h"
#include "critbit.h"
#include "undo.h"

#define JUMPRING_LEN 16

typedef struct _my_glyph_info_t {
	uint32_t code;
	uint8_t color;
	uint16_t status;

	double kerning_correction;
	double x_advance;

	unsigned long glyph_index;
	uint8_t fontidx;
	double x;
	double y;
} my_glyph_info_t;

enum appjumps { APPJUMP_INPUT = 0, APPJUMP_LEN };

enum select_type { BST_NORMAL = 0, BST_WORDS, BST_LINES };

typedef struct _buffer_t {
	char *path;
	char *wd;
	int has_filename;
	int editable;
	uint8_t default_color;
	int inotify_wd;
	time_t mtime;
	bool stale;
	bool single_line;
	int invalid, total; // count of characters

	pthread_rwlock_t rwlock;
	volatile bool release_read_lock;

	volatile int lexy_running;
	volatile int lexy_start;
	volatile int lexy_quick_exit;

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
	my_glyph_info_t *buf; size_t size; int cursor; int mark; int gap; size_t gapsz;

	/* Buffer's secondary properties (calculated) */
	double rendered_height;
	double rendered_width;

	/* Cursor and mark*/
	int savedmark;
	enum select_type select_type;

	/* Undo information */
	undo_t undo;

	/* User options */
	double left_margin;
	double right_margin;

	/* Scripting support */
	GHashTable *props;
	char *keyprocessor;
	void (*onchange)(struct _buffer_t *buffer);

	/* Jumplists */
	int wandercount;
	int appjumps[APPJUMP_LEN];
	int curjump, newjump;
	int jumpring[JUMPRING_LEN];

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
	MT_RELW2, // word based relative move (stops at boundaries)
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

char *buffer_directory(buffer_t *buffer);

// returns true if the buffer was modified since last save
bool buffer_modified(buffer_t *buffer);

// save the buffer to its file (if exists, otherwise fails)
void save_to_text_file(buffer_t *buffer);

my_glyph_info_t *bat(buffer_t *encl, int point);

void buffer_change_select_type(buffer_t *buffer, enum select_type select_type);
void buffer_extend_selection_by_select_type(buffer_t *buffer);

// replace current selection with new_text (main editing function)
void buffer_replace_selection(buffer_t *buffer, const char *new_text);

// undo
void buffer_undo(buffer_t *buffer, bool redo);

// returns current selection
void buffer_get_selection(buffer_t *buffer, int *start, int *end);

// converts a selection of line from this buffer into text
char *buffer_lines_to_text(buffer_t *buffer, int start, int end);

// sets character positions if width has changed
void buffer_typeset_maybe(buffer_t *buffer, double width, bool force);

// functions to get screen coordinates of things (yes, I have no idea anymore what the hell they do or are used for)
void line_get_glyph_coordinates(buffer_t *buffer, int point, double *x, double *y);
int buffer_point_from_position(buffer_t *buffer, int start, double x, double y);

// informs buffer that the configuration is changed, reload fonts
void buffer_config_changed(buffer_t *buffer);

// point motion by lines and glyphs
void sort_mark_cursor(buffer_t *buffer);
bool buffer_move_point_line(buffer_t *buffer, int *p, enum movement_type_t type, int arg);
bool buffer_move_point_glyph(buffer_t *buffer, int *p, enum movement_type_t type, int arg);

// function to execute on change
void buffer_set_onchange(buffer_t *buffer, void (*fn)(buffer_t *buffer));

/* writes in r the indent of cursor_line + a newline and the 0 byte */
char *buffer_indent_newline(buffer_t *buffer);

/* internal word autocompletion functions */
void buffer_wordcompl_init_charset(void);
char *buffer_wordcompl_word_at_cursor(buffer_t *buffer);
void buffer_wordcompl_update(buffer_t *buffer, critbit0_tree *cbt, int radius);
char *buffer_cmdcompl_word_at_cursor(buffer_t *buffer);
char *buffer_historycompl_word_at_cursor(buffer_t *buffer);

// removes all text from a buffer
void buffer_aux_clear(buffer_t *buffer);

void buffer_get_extremes(buffer_t *buffer, int *start, int *end);
char *buffer_all_lines_to_text(buffer_t *buffer);
void buffer_select_all(buffer_t *buffer);
void buffer_wordcompl_update_line(int position, critbit0_tree *c);
char *buffer_get_selection_text(buffer_t *buffer);

int parmatch_find(buffer_t *buffer, int cursor, int nlines, bool forward_only);
my_glyph_info_t *buffer_next_glyph(buffer_t *buffer, my_glyph_info_t *glyph);

int buffer_line_of(buffer_t *buffer, int p, bool fastbail);
int buffer_column_of(buffer_t *buffer, int p);

/* Jump ring management functions */
void buffer_record_jump(buffer_t *buffer);
void buffer_jump_to(buffer_t *buffer, int dir);
double round_to_line(buffer_t *buffer, double v);

#define BSIZE(x) ((x)->size - (x)->gapsz)

#define WORDCOMPL_UPDATE_RADIUS 50000

pid_t buffer_get_child_pid(buffer_t *buffer);

#endif
