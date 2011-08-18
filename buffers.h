#ifndef __BUFFERS_H__
#define __BUFFERS_H__

#include "buffer.h"

#include "editor.h"

#include <gtk/gtk.h>

void buffers_init(void);
void buffers_add(buffer_t *buffer);
void buffers_free(void);

buffer_t *buffers_open(const char *filename, char **rp);

// returns non-zero if close was successful, zero if the user cancelled the action
int buffers_close(buffer_t *buffer);

buffer_t *buffers_get_replacement_buffer(buffer_t *buffer);

void buffers_show_window(editor_t *editor);

#endif
