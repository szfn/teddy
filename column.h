#ifndef __COLUMN_H__
#define __COLUMN_H__

#include <gtk/gtk.h>

#include "editor.h"
#include "buffer.h"

#define GTK_TYPE_COLUMN (gtk_column_get_type())
#define GTK_COLUMN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_COLUMN, column_t))
#define GTK_COLUMN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_COLUMN, column_class))
#define GTK_IS_COLUMN(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_COLUMN))
#define GTK_COLUMN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_COLUMN, column_class))

typedef struct _column_t {
	GtkBox box;
	editor_t **editors;
	int editors_allocated;
	GtkWidget *editors_window;
	int exposed;
	double fraction;
} column_t;

typedef struct _column_class {
	GtkBoxClass parent_class;
} column_class;

GType gtk_column_get_type(void) G_GNUC_CONST;

column_t *column_new(GtkWidget *window, gint spacing);
void column_free(column_t *column);
editor_t *column_new_editor(column_t *column, buffer_t *buffer);
editor_t *column_find_buffer_editor(column_t *column, buffer_t *buffer);
void column_post_show_setup(column_t *column);
editor_t *column_remove(column_t *column, editor_t *editor);
int column_remove_others(column_t *column, editor_t *editor);
void column_replace_buffer(column_t *column, buffer_t *buffer);
editor_t *column_get_editor_before(column_t *column, editor_t *editor);
int column_editor_count(column_t *column);
editor_t *column_get_first_editor(column_t *column);
editor_t *column_get_last_editor(column_t *column);
editor_t *column_get_editor_from_position(column_t *column, double x, double y);
double column_get_occupied_space(column_t *column);
bool column_editor_exists(column_t *column, editor_t *editor);

#endif
