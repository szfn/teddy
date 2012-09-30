#ifndef __LEXY_H__
#define __LEXY_H__

#include <tcl.h>

#include <stdbool.h>

#include "buffer.h"

#define LEXY_STATUS_NUMBER 0xff

void lexy_init(void);
void lexy_update_starting_at(buffer_t *buffer, real_line_t *start_line, bool quick_exit);
void lexy_update_for_move(buffer_t *buffer, real_line_t *possible_start_line);

int lexy_create_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int lexy_append_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

int lexy_assoc_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

int lexy_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int lexy_token_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

const char *lexy_get_link_fn(buffer_t *buffer);


#endif