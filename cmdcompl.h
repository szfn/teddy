#ifndef __CMDCOMPL_H__
#define __CMDCOMPL_H__

#include "editor.h"

extern const char *command_list[];

void cmdcompl_init(void);
void cmdcompl_free(void);
int cmdcompl_complete(const char *text, int length, char *working_directory);
void cmdcompl_show(editor_t *editor, int cursor_position);
void cmdcompl_hide(void);
int cmdcompl_isvisible(void);

void cmdcompl_move_to_prev(void);
void cmdcompl_move_to_next(void);

char *cmdcompl_get_completion(const char *text, int *point);

#endif
