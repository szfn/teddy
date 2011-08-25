#ifndef __COLUMNS_H__
#define __COLUMNS_H__

#include <gtk/gtk.h>

#include "buffer.h"
#include "editor.h"

void columns_init(GtkWidget *window);
void columns_free(void);
editor_t *columns_new(buffer_t *buffer);
void columns_replace_buffer(buffer_t *buffer);


#endif
