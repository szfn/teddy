#ifndef __INTERP__
#define __INTERP__

#include "editor.h"

enum deferred_action {
    NOTHING,
    CLOSE_EDITOR,
    FOCUS_ALREADY_SWITCHED
};

extern enum deferred_action deferred_action_to_return;

void interp_init(void);
void interp_free(void);
enum deferred_action interp_eval(editor_t *editor, const char *command);
void read_conf(void);

extern editor_t *context_editor;

#endif
