#ifndef __BAUX_H__
#define __BAUX_H__

#include "buffer.h"

/* Moves cursor to first non-whitespace character */
void buffer_aux_go_first_nonws(buffer_t *buffer);

/* Moves cursor to first non-whitespace character, if cursor is already there goes to character 0 */
void buffer_aux_go_first_nonws_or_0(buffer_t *buffer);

void buffer_aux_go_end(buffer_t *buffer);
void buffer_aux_go_char(buffer_t *buffer, int n);
void buffer_aux_go_line(buffer_t *buffer, int n);

/* If it is at the beginning of a word (or inside a word) goes to the end of this word, if it is at the end of a word (or inside a non-word sequence) goes to the beginning of the next one */
void buffer_aux_wnwa_next(buffer_t *buffer);

/* If it is at the beginning of a word (or inside a non-word sequence) goes to the end of the previous word, if it is at the end of a word (or inside a word) goes to the beginning of the word) */
void buffer_aux_wnwa_prev(buffer_t *buffer);

#endif
