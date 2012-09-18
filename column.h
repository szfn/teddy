#ifndef __COLUMN_H__
#define __COLUMN_H__

#include <gtk/gtk.h>

#include "tframe.h"

#define GTK_TYPE_COLUMN (gtk_column_get_type())
#define GTK_COLUMN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_COLUMN, column_t))
#define GTK_COLUMN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_COLUMN, column_class))
#define GTK_IS_COLUMN(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_COLUMN))
#define GTK_COLUMN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_COLUMN, column_class))

struct _column_t;
typedef struct _column_t column_t;

GType gtk_column_get_type(void) G_GNUC_CONST;

column_t *column_new(gint spacing);

void column_add_after(column_t *column, tframe_t *before_tf, tframe_t *tf);
bool column_remove(column_t *column, tframe_t *frame, bool reparenting);
int column_hide_others(column_t *column, tframe_t *frame);

double column_fraction(column_t *column);
void column_fraction_set(column_t *column, double fraction);

bool column_find_frame(column_t *column, tframe_t *tf, tframe_t **before_tf, tframe_t **after_tf);
int column_frame_number(column_t *column);

void gtk_column_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

tframe_t *column_get_frame_from_position(column_t *column, double x, double y, bool *ontag);

bool column_close(column_t *column);

void column_expand_frame(column_t *column, tframe_t *frame);

#endif
