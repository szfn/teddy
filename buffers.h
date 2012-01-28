#ifndef __BUFFERS_H__
#define __BUFFERS_H__

#include "buffer.h"

#include "editor.h"

#include <gtk/gtk.h>

void buffers_init(void);
void buffers_add(buffer_t *buffer);
void buffers_free(void);

buffer_t *null_buffer(void);

// returns non-zero if close was successful, zero if the user cancelled the action
int buffers_close(buffer_t *buffer, GtkWidget *window);
int buffers_close_all(GtkWidget *window);

void buffers_show_window(editor_t *editor);

buffer_t *buffers_find_buffer_from_path(const char *path);

buffer_t *buffers_get_buffer_for_process(void);
buffer_t *buffers_create_with_name(char *name);


#endif
