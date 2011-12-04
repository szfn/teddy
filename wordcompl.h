#ifndef __WORDCOMPL_H__
#define __WORDCOMPL_H__

#include "buffer.h"
#include <tcl.h>

#define WORDCOMPL_UPDATE_RADIUS 100

void wordcompl_init(void);
void wordcompl_update(buffer_t *buffer);
void wordcompl_complete(buffer_t *buffer);

int teddy_wordcompl_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif