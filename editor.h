#ifndef __EDITOR_H__
#define __EDITOR_H__

#include "buffer.h"

#include <gtk/gtk.h>

#include "reshandle.h"

typedef struct _editor_t {
    buffer_t *buffer;
    GtkWidget *window;

    GtkObject *adjustment, *hadjustment;
    GtkWidget *drar;
    GtkWidget *drarhscroll;
    GtkIMContext *drarim;
    gboolean cursor_visible;
    int initialization_ended;
    int mouse_marking;

    GtkWidget *table;
    reshandle_t *reshandle;
    GtkWidget *label;
    GtkWidget *entry;
    const char *label_state;
    gboolean search_mode;
    gulong current_entry_handler_id;
    guint timeout_id;
    gboolean search_failed;

    struct _column_t *column;
} editor_t;

editor_t *new_editor(GtkWidget *window, struct _column_t *column, buffer_t *buffer);
void editor_free(editor_t *editor);
void editor_switch_buffer(editor_t *editor, buffer_t *buffer);
gint editor_get_height_request(editor_t *editor);

#endif
