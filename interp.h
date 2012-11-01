#ifndef __INTERP__
#define __INTERP__

#include "editor.h"
#include "buffer.h"

#include <tcl.h>

extern Tcl_Interp *interp;

void interp_init(void);
void interp_free(void);
int interp_eval(editor_t *editor, buffer_t *buffer, const char *command, bool show_ret);
void read_conf(void);

const char *interp_eval_command(editor_t *editor, buffer_t *buffer, int count, const char *argv[]);

/*void interp_context_editor_set(editor_t *editor);
void interp_context_buffer_set(buffer_t *buffer);*/

editor_t *interp_context_editor(void);
buffer_t *interp_context_buffer(void);

void interp_return_point_pair(buffer_t *buffer, int mark, int cursor);
void interp_frame_debug();
bool interp_toplevel_frame();

#define ARGNUM(expr, name) { \
	if (expr) { \
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to '" name "' command");\
		return TCL_ERROR;\
	} \
}

#define HASED(name) { \
	if (interp_context_editor() == NULL) { \
		Tcl_AddErrorInfo(interp, "No editor open, can not execute '" name "' command"); \
		return TCL_ERROR; \
	} \
}

#define HASBUF(name) { \
	if (interp_context_buffer() == NULL) { \
		Tcl_AddErrorInfo(interp, "No editor open, can not execute '" name "' command"); \
		return TCL_ERROR; \
	} \
}

#endif
