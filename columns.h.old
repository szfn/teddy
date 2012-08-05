#ifndef __COLUMNS_H__
#define __COLUMNS_H__

#include <gtk/gtk.h>
#include <stdbool.h>

#include "buffer.h"
#include "editor.h"
#include "column.h"

#define GTK_TYPE_COLUMNS (gtk_columns_get_type())
#define GTK_COLUMNS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_COLUMNS, columns_t))
#define GTK_COLUMNS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_COLUMNS, columns_class))
#define GTK_IS_COLUMNS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_COLUMNS))
#define GTK_COLUMNS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_COLUMNS, columns_class))

typedef struct _columns_t {
	GtkBox box;
	column_t **columns;
	int columns_allocated;
	GtkWidget *columns_window;
	column_t *active_column;
} columns_t;

typedef struct _columns_class {
	GtkBoxClass parent_class;
} columns_class;

GType gtk_columns_get_type(void) G_GNUC_CONST;

columns_t *the_columns_new(GtkWidget *window);
void columns_free(columns_t *columns);
editor_t *columns_new(columns_t *columns, buffer_t *buffer);
editor_t *columns_new_after(columns_t *columns, column_t *column, buffer_t *buffer);
void columns_replace_buffer(columns_t *columns, buffer_t *buffer);
column_t *columns_get_column_before(columns_t *columns, column_t *column);
int columns_column_count(columns_t *columns);
column_t *columns_remove(columns_t *columns, column_t *column, editor_t *editor);
void columns_remove_others(columns_t *columns, column_t *column, editor_t *editor);
editor_t *columns_get_buffer(columns_t *columns, buffer_t *buffer);
editor_t *columns_get_editor_from_position(columns_t *columns, double x, double y, bool *ontag);
column_t *columns_get_column_from_position(columns_t *columns, double x, double y);
void columns_swap_columns(columns_t *columns, column_t *cola, column_t *colb);
bool editor_exists(columns_t *columns, editor_t *editor);
void columns_reallocate(columns_t *columns);
void columns_next_editor(columns_t *columns, editor_t *editor);

editor_t *heuristic_new_frame(columns_t *columns, editor_t *spawning_editor, buffer_t *buffer);

extern column_t *active_column; // column where the last edit operation happened

#endif
