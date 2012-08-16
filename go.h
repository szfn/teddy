#ifndef __GO_H__
#define __GO_H__

#include <tcl.h>
#include <stdbool.h>

#include "editor.h"
#include "buffer.h"

enum go_file_failure_reason {
	GFFR_OTHER = 0,
	GFFR_BINARYFILE,
};

void go_init(GtkWidget *window);
int teddy_go_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]);
editor_t *go_to_buffer(editor_t *editor, buffer_t *buffer, bool take_over);
void mouse_open_action(editor_t *editor, lpoint_t *start, lpoint_t *end);
buffer_t *go_file(buffer_t *buffer, const char *filename, bool create, enum go_file_failure_reason *gffr);

#endif
