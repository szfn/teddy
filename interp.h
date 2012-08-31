#ifndef __INTERP__
#define __INTERP__

#include "editor.h"
#include "buffer.h"

#include <tcl.h>

extern Tcl_Interp *interp;

void interp_init(void);
void interp_free(void);
int interp_eval(editor_t *editor, const char *command, bool show_ret);
void read_conf(void);

const char *interp_eval_command(int count, const char *argv[]);

void interp_context_editor_set(editor_t *editor);
void interp_context_buffer_set(buffer_t *buffer);

editor_t *interp_context_editor(void);
buffer_t *interp_context_buffer(void);

void interp_return_point_pair(lpoint_t *mark, lpoint_t *cursor);
void interp_frame_debug();
bool interp_toplevel_frame();

#endif
