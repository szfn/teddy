#ifndef __COLUMN_H__
#define __COLUMN_H__

#include <gtk/gtk.h>

#include "editor.h"
#include "buffer.h"

typedef struct _column_t {
    editor_t **editors;
    GtkWidget **resize_elements;
    int editors_allocated;
    GtkWidget *editors_window;
    GtkWidget *editors_vbox;
    int empty;
    int exposed;

    double frame_resize_origin;
} column_t;

column_t *column_new(GtkWidget *window);
void column_free(column_t *column);
editor_t *column_new_editor(column_t *column, buffer_t *buffer);
editor_t *column_find_buffer_editor(column_t *column, buffer_t *buffer);
void column_post_show_setup(column_t *column);
editor_t *column_remove(column_t *column, editor_t *editor);
void column_replace_buffer(column_t *column, buffer_t *buffer);
void column_queue_draw_for_buffer(column_t *column, buffer_t *buffer);

#endif
