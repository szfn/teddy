#ifndef __COLUMNS_H__
#define __COLUMNS_H__

#include <stdbool.h>

#include <gtk/gtk.h>

#include "column.h"
#include "buffer.h"

#define GTK_TYPE_COLUMNS (gtk_columns_get_type())
#define GTK_COLUMNS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_COLUMNS, columns_t))
#define GTK_COLUMNS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_COLUMNS, columns_class))
#define GTK_IS_COLUMNS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_COLUMNS))
#define GTK_COLUMNS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_COLUMNS, columns_class))

struct _columns_t;
typedef struct _columns_t columns_t;

GType gtk_columns_get_type(void) G_GNUC_CONST;

columns_t *columns_new(void);

void columns_add_after(columns_t *columns, column_t *before_col, column_t *col, bool set_fraction);
void columns_append(columns_t *columns, column_t *column, bool set_fraction);
bool columns_remove(columns_t *columns, column_t *column);
int columns_remove_others(columns_t *columns, column_t *column);

bool columns_find_frame(columns_t *columns, tframe_t *tf, column_t **before_col, column_t **frame_col, column_t **after_col, tframe_t **before_tf, tframe_t **after_tf);
tframe_t *columns_get_frame_from_position(columns_t *columns, double x, double y, bool *ontag);

void gtk_columns_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

column_t *columns_active(columns_t *columns);
void columns_set_active(columns_t *columns, column_t *column);

tframe_t *heuristic_new_frame(columns_t *columns, tframe_t *spawning_frame, buffer_t *buffer);

void columns_column_remove(columns_t *columns, column_t *col, tframe_t *frame, bool reparenting);

#endif
