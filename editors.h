#ifndef __EDITORS_H__
#define __EDITORS_H__

#include <gtk/gtk.h>

#include "editor.h"
#include "buffer.h"

void editors_init(GtkWidget *window);
void editors_free(void);
editor_t *editors_new(buffer_t *buffer);
editor_t *editors_find_buffer_editor(buffer_t *buffer);
void editors_post_show_setup(void);
editor_t *editors_remove(editor_t *editor);
void editors_replace_buffer(buffer_t *buffer);

#endif
