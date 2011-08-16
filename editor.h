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
    gboolean current_entry_handler_id_set;
    gboolean search_failed;
} editor_t;

editor_t *new_editor(GtkWidget *window, buffer_t *buffer);
void editor_free(editor_t *editor);
void editor_post_show_setup(editor_t *editor);

#endif
