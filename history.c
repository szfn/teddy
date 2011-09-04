#include "history.h"

#include <stdlib.h>

#include "editor.h"

history_t *history_new(void) {
    history_t *r = malloc(sizeof(history_t));

    r->history_tree = gtk_tree_view_new();
    r->history_list = gtk_list_store_new(1, G_TYPE_STRING);

    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(r->history_tree), -1, "Entry", gtk_cell_renderer_text_new(), "text", 0, NULL);
    gtk_tree_view_set_model(GTK_TREE_VIEW(r->history_tree), GTK_TREE_MODEL(r->history_list));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(r->history_tree), FALSE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(r->history_tree), 1);

    r->history_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(r->history_window), r->history_tree);

    gtk_window_set_default_size(GTK_WINDOW(r->history_window), 400, 300);

    //TODO: link select event of tree to close dialog
    
    return r;
}

void history_add(history_t *history, const char *text) {
    GtkTreeIter mah;
    gtk_list_store_insert(history->history_list, &mah, 0);
    gtk_list_store_set(history->history_list, &mah, 0, text, -1);
    printf("Added: [%s]\n", text);
}

const char *history_pick(history_t *history, editor_t *editor) {
    gtk_window_set_transient_for(GTK_WINDOW(history->history_window), GTK_WINDOW(editor->window));
    gtk_window_set_modal(GTK_WINDOW(history->history_window), TRUE);
    gtk_widget_show_all(history->history_window);
    gtk_widget_grab_focus(history->history_tree);
    //TODO: run dialog, return selected entry
    return NULL;
}


