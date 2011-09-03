#ifndef __CMDCOMPL_H__
#define __CMDCOMPL_H__

#include "editor.h"

extern const char *command_list[];
extern char **list_external_commands;
extern int external_commands_allocated;
extern int external_commands_cap;

void cmdcompl_init(void);
void cmdcompl_free(void);
void cmdcompl_complete(const char *text, int length);
void cmdcompl_show(editor_t *editor, int cursor_position);
void cmdcompl_hide(void);
int cmdcompl_isvisible(void);

void cmdcompl_move_to_prev(void);
void cmdcompl_move_to_next(void);

char *cmdcompl_get_completion(const char *text, int *point);

#endif
