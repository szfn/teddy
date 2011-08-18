#ifndef __BUFFERS_H__
#define __BUFFERS_H__

#include "buffer.h"

#include "editor.h"

#include <gtk/gtk.h>

void buffers_init(void);
void buffers_add(buffer_t *buffer);
void buffers_free(void);
void buffers_close(buffer_t *buffer);

void buffers_show_window(editor_t *editor);

#endif
