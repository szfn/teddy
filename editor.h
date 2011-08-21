#ifndef __EDITOR_H__
#define __EDITOR_H__

#include "buffer.h"

#include <gtk/gtk.h>

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
    GtkWidget *label;
    GtkWidget *entry;
    const char *label_state;
    gboolean search_mode;
    gulong current_entry_handler_id;
    gboolean search_failed;

    // stuff used to calculate space requirements
    int allocated_vertical_space;
} editor_t;

editor_t *new_editor(GtkWidget *window, buffer_t *buffer);
void editor_free(editor_t *editor);
void editor_switch_buffer(editor_t *editor, buffer_t *buffer);
gint editor_get_height_request(editor_t *editor);

#endif
