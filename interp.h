#ifndef __INTERP__
#define __INTERP__

#include "editor.h"

void interp_init(void);
void interp_eval(editor_t *editor, const char *command);

#endif
