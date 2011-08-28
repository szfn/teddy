#ifndef __GO_H__
#define __GO_H__

#include <tcl.h>

#include "editor.h"

void go_init(GtkWidget *window);
int acmacs_go_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
editor_t *go_to_buffer(editor_t *editor, buffer_t *buffer);


#endif
