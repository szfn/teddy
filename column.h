#ifndef __COLUMN_H__
#define __COLUMN_H__

#include <gtk/gtk.h>

#include "editor.h"
#include "buffer.h"

typedef struct _column_t {
    editor_t **editors;
    int editors_allocated;
    GtkWidget *editors_window;
    GtkWidget *editors_vbox;
    int exposed;
} column_t;

column_t *column_new(GtkWidget *window);
void column_free(column_t *column);
editor_t *column_new_editor(column_t *column, buffer_t *buffer);
editor_t *column_find_buffer_editor(column_t *column, buffer_t *buffer);
void column_post_show_setup(column_t *column);
editor_t *column_remove(column_t *column, editor_t *editor);
void column_replace_buffer(column_t *column, buffer_t *buffer);
editor_t *column_get_editor_before(column_t *column, editor_t *editor);
int column_editor_count(column_t *column);
editor_t *column_get_first_editor(column_t *column);

#endif
