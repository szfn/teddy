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
editor_t *columns_get_buffer(buffer_t *buffer);
editor_t *columns_get_editor_from_positioon(double x, double y);
column_t *columns_get_column_from_position(double x, double y);
void columns_swap_columns(column_t *cola, column_t *colb);

#endif
