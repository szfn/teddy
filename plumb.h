#ifndef __PLUMB_H__
#define __PLUMB_H__

#include <stdbool.h>

#include <tcl.h>

#include "buffer.h"

void plumb(buffer_t *buffer, bool islink, const char *text);
int teddy_plumb_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif
