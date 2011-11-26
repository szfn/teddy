#include "buffers.h"

#include "global.h"
#include "columns.h"
#include "editor.h"
#include "go.h"

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

int process_buffers_counter = 0;

buffer_t *null_buffer(void) {
	return buffers[0];
}

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
			gtk_widget_hide(buffers_window);
			go_to_buffer(buffers_selector_focus_editor, buffers[idx]);
			return TRUE;
		}
		case GDK_KEY_Escape:
			gtk_widget_hide(buffers_window);
			return TRUE;
		case GDK_KEY_Delete: {
			int idx = get_selected_idx();
			if (idx < 0) return TRUE;
			buffers_close(buffers[idx], NULL);
			gtk_widget_queue_draw(buffers_tree);
			return TRUE;
		}
		}
	}

	return FALSE;
}

#define SAVE_AND_CLOSE_RESPONSE 1
#define DISCARD_CHANGES_RESPONSE 2
#define CANCEL_ACTION_RESPONSE 3

int buffers_close(buffer_t *buffer, GtkWidget *window) {
	if (buffers[0] == buffer) return 1;
                                  
	if (buffer->modified) {
		GtkWidget *dialog;
		GtkWidget *content_area;
		GtkWidget *label;
		char *msg;
		gint result;

		if (buffer->has_filename) {
			dialog = gtk_dialog_new_with_buttons("Close Buffer", (window != NULL) ? GTK_WINDOW(window) : GTK_WINDOW(buffers_selector_focus_editor->window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Save and close", SAVE_AND_CLOSE_RESPONSE, "Discard changes", DISCARD_CHANGES_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);
		} else {
			dialog = gtk_dialog_new_with_buttons("Close Buffer", (window != NULL) ? GTK_WINDOW(window) : GTK_WINDOW(buffers_selector_focus_editor->window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, "Discard changes", DISCARD_CHANGES_RESPONSE, "Cancel", CANCEL_ACTION_RESPONSE, NULL);
		}

		content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
		asprintf(&msg, "Buffer [%s] is modified", buffer->name);
		label = gtk_label_new(msg);
		free(msg);

		gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);

		//g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
		gtk_widget_show_all(dialog);
		result = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		//printf("Response is: %d (%d %d %d)\n", result, SAVE_AND_CLOSE_RESPONSE, DISCARD_CHANGES_RESPONSE, CANCEL_ACTION_RESPONSE);

		switch(result) {
		case SAVE_AND_CLOSE_RESPONSE:
			save_to_text_file(buffer);
			break;
		case DISCARD_CHANGES_RESPONSE:
			//printf("Discarding changes to: [%s]\n", buffer->name);
			break;
		case CANCEL_ACTION_RESPONSE: return 0;
		default: return 0; /* This shouldn't happen */
		}
	}

	columns_replace_buffer(buffer);

	{
		int i;
		for(i = 0; i < buffers_allocated; ++i) {
			if (buffers[i] == buffer) break;
		}

		if (i < buffers_allocated) {
			GtkTreeIter mah;

			if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(buffers_list), &mah)) {
				do {
					GValue value = {0};

					gtk_tree_model_get_value(GTK_TREE_MODEL(buffers_list), &mah, 0, &value);
					if (g_value_get_int(&value) == i) {
						g_value_unset(&value);
						gtk_list_store_remove(buffers_list, &mah);
						break;
					} else {
						g_value_unset(&value);
					}
				} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(buffers_list), &mah));
			}
			
			buffers[i] = NULL;

			gtk_widget_queue_draw(buffers_tree);
		} else {
			printf("Attempted to remove buffer not present in list\n");
		}
		
		buffer_free(buffer);
	}

	return 1;
}

void buffers_init(void) {
	process_buffers_counter = 0;
	
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

	buffers[0] = buffer_create(&library);
	{
		free(buffers[0]->name);
		asprintf(&(buffers[0]->name), "+null+");
		load_empty(buffers[0]);
		buffers[0]->editable = 0;
	}

	{
		GtkWidget *vbox = gtk_vbox_new(FALSE, 2);
		GtkWidget *label = gtk_label_new("Buffers:");
		GtkWidget *label2 = gtk_label_new("Press <Enter> to focus buffer, <Del> to delete buffer,\n<Esc> to close");
		GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);

		buffers_list = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
		buffers_tree = gtk_tree_view_new();

		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(buffers_tree), -1, "Buffer Number", gtk_cell_renderer_text_new(), "text", 0, NULL);
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(buffers_tree), -1, "Buffer Name", gtk_cell_renderer_text_new(), "text", 1, NULL);
		gtk_tree_view_set_model(GTK_TREE_VIEW(buffers_tree), GTK_TREE_MODEL(buffers_list));
		gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(buffers_tree), FALSE);
		gtk_tree_view_set_search_column(GTK_TREE_VIEW(buffers_tree), 1);

		gtk_container_add(GTK_CONTAINER(scroll), buffers_tree);

		gtk_container_add(GTK_CONTAINER(vbox), label);
		gtk_container_add(GTK_CONTAINER(vbox), scroll);
		gtk_container_add(GTK_CONTAINER(vbox), label2);

		gtk_box_set_child_packing(GTK_BOX(vbox), label, FALSE, FALSE, 2, GTK_PACK_START);
		gtk_box_set_child_packing(GTK_BOX(vbox), buffers_tree, TRUE, TRUE, 2, GTK_PACK_START);
		gtk_box_set_child_packing(GTK_BOX(vbox), label2, FALSE, FALSE, 2, GTK_PACK_END);

		gtk_label_set_justify(GTK_LABEL(label2), GTK_JUSTIFY_LEFT);
		
		buffers_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_decorated(GTK_WINDOW(buffers_window), TRUE);

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

int buffers_close_all(GtkWidget *window) {
	int i;
	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (!buffers_close(buffers[i], window)) return 0;
	}
	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (buffers[i]->modified) return 0;
	}
	return 1;
}


buffer_t *buffers_open(buffer_t *base_buffer, const char *filename, char **rp) {
	buffer_t *b;
	*rp = unrealpath((base_buffer != NULL) ? base_buffer->path : NULL, filename);

	if (rp == NULL) {
		perror("Error resolving pathname");
		return NULL;
	}

	b = buffer_create(&library);
	if (load_text_file(b, *rp) != 0) {
		// file may not exist, attempt to create it
		FILE *f = fopen(*rp, "w");
			
		if (!f) {
			buffer_free(b);
			return NULL;
		}
			
		fclose(f);
		if (load_text_file(b, *rp) != 0) {
			buffer_free(b);
			return NULL;
		}
	}
		
	// file loaded or created successfully
	buffers_add(b);
	return b;
}

buffer_t *buffers_find_buffer_from_path(const char *urp) {
	char *rp = realpath(urp, NULL);
	buffer_t *r = NULL;
	int i;

	if (rp == NULL) {
		return NULL;
	}

	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (!(buffers[i]->has_filename)) continue;
		if (buffers[i]->path == NULL) continue;
		if (strcmp(buffers[i]->path, rp) == 0) {
			r = buffers[i];
			break;
		}
	}

	free(rp);
	return r;
}

buffer_t *buffers_get_buffer_for_process(void) {
	buffer_t *buffer;
	int i;
	// look for a buffer with a name starting by +bg/ that doesn't have a process
	for (i = 0; i < buffers_allocated; ++i) {
		if (buffers[i] == NULL) continue;
		if (strncmp(buffers[i]->name, "+bg/", 4) != 0) continue;
		if (buffers[i]->job != NULL) continue;
		break;
	}

	if (i >= buffers_allocated) {
		buffer = buffer_create(&library);
		load_empty(buffer);
		if (buffer->name != NULL) {
			free(buffer->name);
		}

		asprintf(&(buffer->name), "+bg/%d+", process_buffers_counter);
		++process_buffers_counter;
		buffers_add(buffer);
	} else {
		buffer = buffers[i];
	}

	return buffer;
}

