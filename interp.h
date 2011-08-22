#ifndef __INTERP__
#define __INTERP__

#include "editor.h"

enum deferred_action {
    NOTHING,
    CLOSE_EDITOR
};

void interp_init(void);
void interp_free(void);
enum deferred_action interp_eval(editor_t *editor, const char *command);

#endif
