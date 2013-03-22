#ifndef __LEXY_H__
#define __LEXY_H__

#include <tcl.h>

#include <stdbool.h>

#include "buffer.h"

#define LEXY_ROWS 0xffff

void lexy_init(void);
int lexy_find_status(const char *name);
int lexy_start_status_for_buffer(buffer_t *buffer);
void lexy_update_starting_at(buffer_t *buffer, int start, bool quick_exit);
void lexy_update_resume(buffer_t *buffer);

int lexy_create_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int lexy_append_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

int lexy_assoc_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);

int lexy_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int lexy_token_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
int lexy_parse_token(int state, const char *text, char **file, char **line, char **col);
const char *deparse_token_type_name(int r);


const char *lexy_get_link_fn(buffer_t *buffer);


#endif