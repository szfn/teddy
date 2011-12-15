#ifndef __LEXY_H__
#define __LEXY_H__

#include <tcl.h>

#include "buffer.h"

#define L_NOTHING 0
#define L_KEYWORD 1
#define L_ID 2
#define L_COMMENT 3
#define L_STRING 4

void lexy_init(void);
void lexy_update(buffer_t *buffer);

int lexy_create_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int lexy_append_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

int lexy_assoc_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

int lexy_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

#endif