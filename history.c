#include "history.h"

#include <stdlib.h>

#include "editor.h"
#include "interp.h"
#include "global.h"

static gboolean history_select_callback(GtkTreeView *widget, GtkTreePath *path, GtkTreeViewColumn *column, history_t *history) {
	gtk_dialog_response(GTK_DIALOG(history->history_window), 10);
	return TRUE;
}

history_t *history_new(void) {
	history_t *r = malloc(sizeof(history_t));

	r->history_tree = gtk_tree_view_new();
	r->history_list = gtk_list_store_new(1, G_TYPE_STRING);

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(r->history_tree), -1, "Entry", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(r->history_tree), GTK_TREE_MODEL(r->history_list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(r->history_tree), FALSE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(r->history_tree), 1);

	r->history_window = gtk_dialog_new();
	gtk_window_set_destroy_with_parent(GTK_WINDOW(r->history_window), TRUE);
	
	GtkWidget *scroll_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll_window), r->history_tree);
	
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(r->history_window))), scroll_window);

	gtk_window_set_default_size(GTK_WINDOW(r->history_window), 400, 300);

	g_signal_connect(G_OBJECT(r->history_window), "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(G_OBJECT(r->history_tree), "row-activated", G_CALLBACK(history_select_callback), r);
	
	return r;
}

void history_add(history_t *history, const char *text) {
	GtkTreeIter mah;
	gtk_list_store_insert(history->history_list, &mah, 0);
	gtk_list_store_set(history->history_list, &mah, 0, text, -1);
}

int teddy_history_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	history_t *history;
	int idx, i;
	gboolean valid;
	GtkTreeIter iter;
	
	if (context_editor == NULL) {
		Tcl_AddErrorInfo(interp, "Can not call 'teddyhistory' command without a current editor");
		return TCL_ERROR;
	}
	
	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'teddyhistory' command");
		return TCL_ERROR;
	}
	
	if (strcmp(argv[1], "cmd") == 0) {
		history = command_history;
	} else if (strcmp(argv[1], "search") == 0) {
		history = search_history;
	} else {
		Tcl_AddErrorInfo(interp, "Wrong first argument for 'teddyhistory' command, must be 'cmd' or 'search'");
		return TCL_ERROR;
	}
	
	idx = atoi(argv[2]);
	
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(history->history_list), &iter);
	
	for (i = 1; i < idx; ++i) {
       if (!valid) break;
       valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(history->history_list), &iter);
	}
	
	if (valid) {
		GValue value = {0};
		const char *pick;
		
		gtk_tree_model_get_value(GTK_TREE_MODEL(history->history_list), &iter, 0, &value);
		
		pick = g_value_get_string(&value);
		
		if (pick != NULL) {
           Tcl_SetResult(interp, (char *)pick, TCL_VOLATILE);
		} else {
           Tcl_SetResult(interp, "", TCL_VOLATILE);
		}
		
		g_value_unset(&value);
	} else {
		Tcl_SetResult(interp, "", TCL_VOLATILE);
	}
	
	return TCL_OK;
}

void history_pick(history_t *history, editor_t *editor) {
	int r;
	
	gtk_window_set_transient_for(GTK_WINDOW(history->history_window), GTK_WINDOW(editor->window));
	gtk_window_set_modal(GTK_WINDOW(history->history_window), TRUE);
	gtk_widget_show_all(history->history_window);
	gtk_widget_grab_focus(history->history_tree);
	r = gtk_dialog_run(GTK_DIALOG(history->history_window));
	gtk_widget_hide(history->history_window);

	if (r == 10) {
		GtkTreePath *focus_path;
		GtkTreeIter iter;
		const char *pick;
		GValue value = {0};
		
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(history->history_tree), &focus_path, NULL);
		if (focus_path == NULL) {
			gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(history->history_list), &iter);
			if (!valid) {
				return;
			}
		} else {
			gtk_tree_model_get_iter(GTK_TREE_MODEL(history->history_list), &iter, focus_path);
		}
		
		gtk_tree_model_get_value(GTK_TREE_MODEL(history->history_list), &iter, 0, &value);
		
		pick = g_value_get_string(&value);

		gtk_entry_set_text(GTK_ENTRY(editor->entry), pick);

		if (focus_path != NULL) {
			gtk_tree_path_free(focus_path);
		}
		
		g_value_unset(&value);
	}
}
