#include "buffers.h"

#include "editor.h"

#include <gdk/gdkkeysyms.h>
#include <glib-object.h>

#include <stdlib.h>
#include <stdio.h>

buffer_t **buffers;
int buffers_allocated;
GtkWidget *buffers_window;
GtkListStore *buffers_list;
GtkWidget *buffers_tree;
editor_t *buffers_selector_focus_editor;

static int get_selected_idx(void) {
    GtkTreePath *focus_path;
    GtkTreeViewColumn *focus_column;
    GtkTreeIter iter;
    GValue value = {0};
    int idx;
    
    gtk_tree_view_get_cursor(GTK_TREE_VIEW(buffers_tree), &focus_path, &focus_column);

    if (focus_path == NULL) return -1;

    gtk_tree_model_get_iter(GTK_TREE_MODEL(buffers_list), &iter, focus_path);
    gtk_tree_model_get_value(GTK_TREE_MODEL(buffers_list), &iter, 0, &value);
    idx = g_value_get_int(&value);
    
    g_value_unset(&value);
    gtk_tree_path_free(focus_path);

    if ((idx >= buffers_allocated) || (buffers[idx] == NULL)) {
        printf("Error selecting buffer (why?) %d\n", idx);
        return -1;
    } 
    
    return idx;
}

static gboolean buffers_key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    int shift = event->state & GDK_SHIFT_MASK;
    int ctrl = event->state & GDK_CONTROL_MASK;
    int alt = event->state & GDK_MOD1_MASK;
    int super = event->state & GDK_SUPER_MASK;

    if (!shift && !ctrl && !alt && !super) {
        switch(event->keyval) {
        case GDK_KEY_Return: {
            int idx = get_selected_idx();
            if (idx < 0) return TRUE;
            editor_switch_buffer(buffers_selector_focus_editor, buffers[idx]);
            gtk_widget_hide(buffers_window);
            return TRUE;
        }
        case GDK_KEY_Escape:
            gtk_widget_hide(buffers_window);
            return TRUE;
        case GDK_KEY_Delete: {
            int idx = get_selected_idx();
            if (idx < 0) return TRUE;
            buffers_close(buffers[idx]);
            gtk_widget_queue_draw(buffers_tree);
            return TRUE;
        }
        }
    }

    return FALSE;
}

void buffers_close(buffer_t *buffer) {
    if (buffer->modified) {
        //TODO: ask for permission to close
    }

    //TODO: actually close
}

void buffers_init(void) {
    {
        int i;
        buffers_allocated = 10;
        buffers = malloc(sizeof(buffer_t *) * buffers_allocated);
        
        for (i = 0; i < buffers_allocated; ++i) {
            buffers[i] = NULL;
        }
        
        if (!buffers) {
            perror("Out of memory");
            exit(EXIT_FAILURE);
        }
    }

    {
        GtkWidget *vbox = gtk_vbox_new(FALSE, 2);
        GtkWidget *label = gtk_label_new("Buffers:");
        GtkWidget *label2 = gtk_label_new("Press <Enter> to select, <Del> to delete buffer,\n<Esc> to close");

        buffers_list = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
        buffers_tree = gtk_tree_view_new();

        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(buffers_tree), -1, "Buffer Number", gtk_cell_renderer_text_new(), "text", 0, NULL);
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(buffers_tree), -1, "Buffer Name", gtk_cell_renderer_text_new(), "text", 1, NULL);
        gtk_tree_view_set_model(GTK_TREE_VIEW(buffers_tree), GTK_TREE_MODEL(buffers_list));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(buffers_tree), FALSE);
        gtk_tree_view_set_search_column(GTK_TREE_VIEW(buffers_tree), 1);

        gtk_container_add(GTK_CONTAINER(vbox), label);
        gtk_container_add(GTK_CONTAINER(vbox), buffers_tree);
        gtk_container_add(GTK_CONTAINER(vbox), label2);

        gtk_box_set_child_packing(GTK_BOX(vbox), label, FALSE, FALSE, 2, GTK_PACK_START);
        gtk_box_set_child_packing(GTK_BOX(vbox), buffers_tree, TRUE, TRUE, 2, GTK_PACK_START);
        gtk_box_set_child_packing(GTK_BOX(vbox), label2, FALSE, FALSE, 2, GTK_PACK_END);

        gtk_label_set_justify(GTK_LABEL(label2), GTK_JUSTIFY_LEFT);
        
        buffers_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

        gtk_container_add(GTK_CONTAINER(buffers_window), vbox);

        gtk_window_set_default_size(GTK_WINDOW(buffers_window), 400, 300);

        g_signal_connect(G_OBJECT(buffers_window), "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

        g_signal_connect(G_OBJECT(buffers_tree), "key-press-event", G_CALLBACK(buffers_key_press_callback), NULL);
    }
    
}


static void buffers_grow() {
    int i;
    buffers = realloc(buffers, sizeof(buffer_t *) * buffers_allocated * 2);
    if (!buffers) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = buffers_allocated; i < buffers_allocated * 2; ++i) {
        buffers[i] = NULL;
    }
    buffers_allocated *= 2;
}

void buffers_add(buffer_t *b) {
    int i;
    for (i = 0; i < buffers_allocated; ++i) {
        if (buffers[i] == NULL) {
            buffers[i] = b;
            break;
        }
    }

    if (i >= buffers_allocated) {
        buffers_grow();
        buffers_add(b);
        return;
    }

    {
        GtkTreeIter mah;

        gtk_list_store_append(buffers_list, &mah);
        gtk_list_store_set(buffers_list, &mah, 0, i, 1, b->name, -1);
    }
}

void buffers_free(void) {
    int i;
    for (i = 0; i < buffers_allocated; ++i) {
        if (buffers[i] != NULL) {
            buffer_free(buffers[i]);
            buffers[i] = NULL;
        }
    }

    g_object_unref(buffers_list);

    gtk_widget_destroy(buffers_window);
}

void buffers_show_window(editor_t *editor) {
    buffers_selector_focus_editor = editor;
    gtk_window_set_transient_for(GTK_WINDOW(buffers_window), GTK_WINDOW(editor->window));
    gtk_window_set_modal(GTK_WINDOW(buffers_window), TRUE);
    gtk_widget_show_all(buffers_window);
    gtk_widget_grab_focus(buffers_tree);
}
