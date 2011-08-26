#ifndef __COLUMNS_H__
#define __COLUMNS_H__

#include <gtk/gtk.h>

#include "buffer.h"
#include "editor.h"
#include "column.h"

void columns_init(GtkWidget *window);
void columns_free(void);
editor_t *columns_new(buffer_t *buffer);
void columns_replace_buffer(buffer_t *buffer);
column_t *columns_get_column_before(column_t *column);
extern GtkWidget *columns_hbox;
int columns_column_count(void);
column_t *columns_remove(column_t *column, editor_t *editor);

#endif
