#ifndef __TOP_H__
#define __TOP_H__

#include "editor.h"

GtkWidget *top_init(GtkWidget *window);

void top_start_command_line(editor_t *editor, const char *text);
editor_t *top_context_editor(void);
char *top_working_directory(void);
void top_cd(const char *newdir);
bool top_command_line_focused(void);
bool top_has_tags(void);
void top_context_editor_gone(void);
void top_recoloring(void);
void top_message(const char *m);

extern buffer_t *cmdline_buffer;

#endif
