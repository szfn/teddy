#ifndef __BAUX_H__
#define __BAUX_H__

#include "buffer.h"

#include "compl.h"

/* Moves cursor to first non-whitespace character */
void buffer_aux_go_first_nonws(buffer_t *buffer);

/* Moves cursor to first non-whitespace character, if cursor is already there goes to character 0 */
void buffer_aux_go_first_nonws_or_0(buffer_t *buffer);

void buffer_aux_go_end(buffer_t *buffer);
void buffer_aux_go_char(buffer_t *buffer, int n);
void buffer_aux_go_line(buffer_t *buffer, int n);

/* If it is at the beginning of a word (or inside a word) goes to the end of this word, if it is at the end of a word (or inside a non-word sequence) goes to the beginning of the next one */
void buffer_aux_wnwa_next(buffer_t *buffer);

void buffer_aux_wnwa_next_ex(lpoint_t *point);

/* If it is at the beginning of a word (or inside a non-word sequence) goes to the end of the previous word, if it is at the end of a word (or inside a word) goes to the beginning of the word) */
void buffer_aux_wnwa_prev(buffer_t *buffer);

void buffer_aux_wnwa_prev_ex(lpoint_t *point);

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

bool buffer_aux_is_directory(buffer_t *buffer);

void buffer_get_extremes(buffer_t *buffer, lpoint_t *start, lpoint_t *end);
char *buffer_all_lines_to_text(buffer_t *buffer);

#endif
