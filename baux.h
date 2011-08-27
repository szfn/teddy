#ifndef __BAUX_H__
#define __BAUX_H__

#include "buffer.h"

void buffer_aux_go_first_nonws(buffer_t *buffer);
void buffer_aux_go_end(buffer_t *buffer);
void buffer_aux_go_char(buffer_t *buffer, int n);
void buffer_aux_go_line(buffer_t *buffer, int n);

#endif
