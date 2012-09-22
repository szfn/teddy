#ifndef __BAUX_H__
#define __BAUX_H__

#include "buffer.h"

#include "compl.h"

enum movement_type_t {
	MT_ABS = 0, // move to absolute line/column
	MT_REL, // relative move
	MT_END, // move to end
	MT_START, // move to first non-whitespace character (buffer_move_point_glyph only)
	MT_HOME, // toggle between first column and first non-whitespace character (buffer_move_point_glyph only)
	MT_RELW, // word based relative move (buffer_move_point_glyph only)
};

bool buffer_move_point_line(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg);
bool buffer_move_point_glyph(buffer_t *buffer, lpoint_t *p, enum movement_type_t type, int arg);

/* writes in r the indent of cursor_line + a newline and the 0 byte */
void buffer_indent_newline(buffer_t *buffer, char *r);

/* adds text to the end of the buffer */
void buffer_append(buffer_t *buffer, const char *msg, int length, int on_new_line);

/* basic indentation manipulation functions */
void buffer_incr_indent(buffer_t *buffer, int count);
void buffer_decr_indent(buffer_t *buffer, int count);

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

#endif
