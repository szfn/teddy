#ifndef __WORDCOMPL_H__
#define __WORDCOMPL_H__

#include "buffer.h"
#include <tcl.h>

#define WORDCOMPL_UPDATE_RADIUS 100
#define MINIMUM_WORDCOMPL_WORD_LEN 3

#define WORDCOMPL_CALLCOUNT_TRIGGER 20
#define WORDCOMPL_CLEANUP_MAX 1

void wordcompl_init(void);
void wordcompl_update(buffer_t *buffer);
void wordcompl_complete(buffer_t *buffer);

int teddy_wordcompl_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int teddy_wordcompl_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif