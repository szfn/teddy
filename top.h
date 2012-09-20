#ifndef __TOP_H__
#define __TOP_H__

#include "editor.h"

GtkWidget *top_init(void);

void top_start_command_line(editor_t *editor, const char *text);
editor_t *top_context_editor(void);
char *top_working_directory(void);
void top_show_status(void);
void top_cd(const char *newdir);
bool top_command_line_focused(void);
bool top_has_tags(void);

#endif
