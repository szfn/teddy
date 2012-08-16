#include "iopen.h"

#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include "editor.h"
#include "baux.h"
#include "top.h"
#include "global.h"
#include "buffer.h"

#define IOPEN_RECURSOR_DEPTH_LIMIT 512
#define IOPEN_MAX_SENT_RESULTS 512

GtkWidget *parent_window;

GtkWidget *iopen_window;
buffer_t *iopen_buffer;
editor_t *iopen_editor;
GtkWidget *files_vbox, *tags_vbox;

GtkListStore *files_list, *tags_list;
GtkWidget *files_tree, *tags_tree;

struct iopen_result {
	char *path;
	double rank;
};

GAsyncQueue *file_recursor_requests;

static gboolean delete_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
	g_async_queue_push(file_recursor_requests, strdup(""));
	gtk_widget_hide(iopen_window);
	return TRUE;
}

static void iopen_escape(editor_t *editor) {
	g_async_queue_push(file_recursor_requests, strdup(""));
	gtk_widget_hide(iopen_window);
}

static void iopen_enter(editor_t *editor) {
	//TODO: implement
}

static bool iopen_other_keys(editor_t *editor, bool shift, bool ctrl, bool alt, bool super, guint keyval) {
	//TODO: manage up, down, left and right
	return false;
}

static gboolean iopen_add_result(struct iopen_result *r) {
	GtkTreeIter mah;
	gtk_list_store_append(files_list, &mah);
	gtk_list_store_set(files_list, &mah, 0, r->path, 1, r->rank, -1);
	free(r->path);
	free(r);
	return FALSE;
}

static void iopen_buffer_onchange(buffer_t *buffer) {
	char *text = buffer_all_lines_to_text(buffer);
	g_async_queue_push(file_recursor_requests, text);
	gtk_list_store_clear(files_list);
	gtk_widget_queue_draw(files_tree);
}

static gpointer iopen_recursor_thread(gpointer data) {
	struct irt_frame {
		DIR *dir;
		struct dirent *cur;
	} tovisit[IOPEN_RECURSOR_DEPTH_LIMIT];
	int top = 0;
	char *request = NULL;

	int count = 0;

	for (;;) {
		char *new_request;

		// if top is 0 there is nothing else to do, we can block waiting for a request
		if (top == 0) {
			//printf("Waiting\n");
			new_request = g_async_queue_pop(file_recursor_requests);
		} else {
			new_request = g_async_queue_try_pop(file_recursor_requests);
		}

		if (new_request != NULL) {
			// got a new request, discard the old one, reset the stack and start back from current directory
			// an empty string as request will stop us if we were doing something

			//printf("New request: <%s>\n", new_request);

			if (request != NULL) free(request);
			request = new_request;

			for (int i = 0; i < top; ++i) {
				closedir(tovisit[i].dir);
			}

			if (strcmp(request, "") == 0) {
				top = 0;
			} else {
				char *wd = get_current_dir_name();
				alloc_assert(wd);
				tovisit[0].dir = opendir(wd);
				tovisit[0].cur = NULL;
				top = 1;
				count = 0;
				free(wd);
			}
		}

		if (top > 0) {
			struct irt_frame *cf = tovisit + (top - 1);
			if (cf->dir == NULL) {
				// open of a directory failed, not a problem
				--top;
				continue;
			}

			cf->cur = readdir(cf->dir);

			if (cf->cur == NULL) {
				// this directory is done, close it and pop the stack
				closedir(cf->dir);
				--top;
				continue;
			}

			if (cf->cur->d_name[0] == '.') {
				continue;
			}

			char *match_start = strcasestr(cf->cur->d_name, request);
			if (match_start != NULL) {
				struct iopen_result *r = malloc(sizeof(struct iopen_result));
				int len = 0;
				for (int i = 0; i < top; ++i) {
					len += strlen(tovisit[i].cur->d_name) + 1;
				}
				if (cf->cur->d_type == DT_DIR) ++len;
				r->path = malloc(sizeof(char) * len);
				r->path[0] = '\0';
				for (int i = 0; i < top; ++i) {
					strcat(r->path, tovisit[i].cur->d_name);
					if (i != top-1) strcat(r->path, "/");
				}
				if (cf->cur->d_type == DT_DIR) strcat(r->path, "/");

				r->rank = (top + ((cf->cur->d_type == DT_DIR) ? 0 : -1))*1000
					+ (match_start - cf->cur->d_name)*10
					+ strlen(cf->cur->d_name);
				g_idle_add((GSourceFunc)iopen_add_result, r);

				if (++count > IOPEN_MAX_SENT_RESULTS) {
					for (int i = 0; i < top; ++i) {
						closedir(tovisit[i].dir);
						top = 0;
					}
					continue;
				}
			}

			if ((cf->cur->d_type == DT_DIR) && (top < IOPEN_RECURSOR_DEPTH_LIMIT)) {
				// recur on this directory
				int newfd = openat(dirfd(cf->dir), cf->cur->d_name, O_RDONLY);
				if (newfd != -1) {
					tovisit[top].dir = fdopendir(newfd);
					tovisit[top].cur = NULL;
					top++;
				}
			}
		}
	}
	return NULL;
}

void iopen_init(GtkWidget *window) {
	parent_window = window;

	GtkWidget *main_vbox = gtk_vbox_new(false, 0);

	iopen_buffer = buffer_create();
	load_empty(iopen_buffer);
	iopen_editor = new_editor(iopen_buffer, true);

	buffer_set_onchange(iopen_buffer, iopen_buffer_onchange);

	config_set(&(iopen_buffer->config), CFG_AUTOWRAP, "0");

	iopen_editor->single_line_escape = &iopen_escape;
	iopen_editor->single_line_return = &iopen_enter;
	iopen_editor->single_line_other_keys = &iopen_other_keys;

	gtk_container_add(GTK_CONTAINER(main_vbox), GTK_WIDGET(iopen_editor));
	gtk_box_set_child_packing(GTK_BOX(main_vbox), GTK_WIDGET(iopen_editor), FALSE, FALSE, 0, GTK_PACK_START);

	GtkWidget *results_hbox = gtk_hbox_new(true, 10);

	files_vbox = gtk_vbox_new(false, 10);

	GtkWidget *files_label = gtk_label_new("Files:");
	gtk_container_add(GTK_CONTAINER(files_vbox), files_label);
	gtk_box_set_child_packing(GTK_BOX(files_vbox), files_label, FALSE, FALSE, 0, GTK_PACK_START);

	files_list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_DOUBLE);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(files_list), 1, GTK_SORT_ASCENDING);
	files_tree = gtk_tree_view_new();

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(files_tree), -1, "Path", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(files_tree), GTK_TREE_MODEL(files_list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(files_tree), FALSE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(files_tree), 1);

	GtkWidget *files_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(files_scroll), files_tree);

	gtk_container_add(GTK_CONTAINER(files_vbox), files_scroll);
	gtk_box_set_child_packing(GTK_BOX(files_vbox), files_scroll, TRUE, TRUE, 0, GTK_PACK_START);

	tags_vbox = gtk_vbox_new(false, 10);

	GtkWidget *tags_label = gtk_label_new("Tags:");
	gtk_container_add(GTK_CONTAINER(tags_vbox), tags_label);
	gtk_box_set_child_packing(GTK_BOX(tags_vbox), tags_label, FALSE, FALSE, 0, GTK_PACK_START);

	tags_list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_DOUBLE);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tags_list), 1, GTK_SORT_ASCENDING);
	tags_tree = gtk_tree_view_new();

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tags_tree), -1, "Path", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(tags_tree), GTK_TREE_MODEL(tags_list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tags_tree), FALSE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(tags_tree), 1);

	GtkWidget *tags_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(tags_scroll), tags_tree);

	gtk_container_add(GTK_CONTAINER(tags_vbox),  tags_scroll);
	gtk_box_set_child_packing(GTK_BOX(tags_vbox), tags_scroll, TRUE, TRUE, 0, GTK_PACK_START);

	gtk_container_add(GTK_CONTAINER(results_hbox), files_vbox);
	gtk_container_add(GTK_CONTAINER(results_hbox), tags_vbox);

	gtk_container_add(GTK_CONTAINER(main_vbox), results_hbox);
	gtk_box_set_child_packing(GTK_BOX(main_vbox), results_hbox, TRUE, TRUE, 0, GTK_PACK_START);

	iopen_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(iopen_window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(iopen_window), 400, 300);

	g_signal_connect(G_OBJECT(iopen_window), "delete-event", G_CALLBACK(delete_callback), NULL);

	gtk_container_add(GTK_CONTAINER(iopen_window), main_vbox);

	file_recursor_requests = g_async_queue_new();
	g_thread_new("iopen file recursion", iopen_recursor_thread, NULL);
}

void iopen(void) {
	gtk_window_set_transient_for(GTK_WINDOW(iopen_window), GTK_WINDOW(parent_window));
	gtk_window_set_modal(GTK_WINDOW(iopen_window), TRUE);

	gtk_list_store_clear(files_list);
	gtk_list_store_clear(tags_list);

	buffer_aux_clear(iopen_buffer);
	iopen_buffer->cursor.line = iopen_buffer->real_line;
	iopen_buffer->cursor.glyph = 0;

	gchar *text = gtk_clipboard_wait_for_text(selection_clipboard);
	if ((text != NULL) && (strcmp(text, "") != 0)) {
		if (strstr(text, "\n") == NULL) {
			buffer_replace_selection(iopen_buffer, text);
			iopen_buffer->mark.line = iopen_buffer->real_line;
			iopen_buffer->mark.glyph = 0;
		}
	}

	if (text != NULL) g_free(text);

	gtk_widget_show_all(iopen_window);
	gtk_widget_set_visible(tags_vbox, top_has_tags());
	gtk_widget_grab_focus(GTK_WIDGET(iopen_editor));
}
