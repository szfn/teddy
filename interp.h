#ifndef __INTERP__
#define __INTERP__

#include "editor.h"
#include "buffer.h"

#include <tcl.h>

enum deferred_action {
	NOTHING,
	CLOSE_EDITOR,
	FOCUS_ALREADY_SWITCHED
};

extern enum deferred_action deferred_action_to_return;
extern Tcl_Interp *interp;

void interp_init(void);
void interp_free(void);
enum deferred_action interp_eval(editor_t *editor, const char *command, bool show_ret);
void read_conf(void);

const char *interp_eval_command(int count, const char *argv[]);

void interp_context_editor_set(editor_t *editor);
void interp_context_buffer_set(buffer_t *buffer);

editor_t *interp_context_editor(void);
buffer_t *interp_context_buffer(void);

#endif
