#ifndef __CMDCOMPL_H__
#define __CMDCOMPL_H__

extern const char *command_list[];
extern char **list_external_commands;
extern int external_commands_allocated;
extern int external_commands_cap;

void cmdcompl_init(void);
void cmdcompl_free(void);
void cmdcompl_complete(const char *text, int length);

#endif
