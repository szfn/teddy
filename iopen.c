#include "iopen.h"

#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "editor.h"
#include "top.h"
#include "global.h"
#include "buffer.h"
#include "tags.h"
#include "research.h"
#include "interp.h"
#include "buffers.h"

#define IOPEN_RECURSOR_DEPTH_LIMIT 128
#define IOPEN_MAX_SENT_RESULTS 128

GtkWidget *parent_window;

GtkWidget *iopen_window;
buffer_t *iopen_buffer;
editor_t *iopen_editor;

GtkListStore *results_list;
GtkWidget *results_tree;

struct iopen_result {
	char *show;
	char *path;
	char *search;
	double rank;
};

GAsyncQueue *file_recursor_requests;
GAsyncQueue *tags_requests;
GAsyncQueue *buffers_requests;

GtkCellRenderer *crt;

static void iopen_close(void) {
	g_async_queue_push(file_recursor_requests, strdup(""));
	g_async_queue_push(tags_requests, strdup(""));
	g_async_queue_push(buffers_requests, strdup(""));
	gtk_widget_hide(iopen_window);
}

static gboolean delete_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
	iopen_close();
	return TRUE;
}

static void iopen_escape(editor_t *editor) {
	iopen_close();
}

static void iopen_open(GtkTreeView *tree, GtkTreePath *treepath) {
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
	GtkTreeIter iter;
	GValue path_value = { 0 }, search_value = { 0 };
	if (!gtk_tree_model_get_iter(model, &iter, treepath)) return;
	gtk_tree_model_get_value(model, &iter, 1, &path_value);
	gtk_tree_model_get_value(model, &iter, 2, &search_value);
	const char *path = g_value_get_string(&path_value);
	const char *search = g_value_get_string(&search_value);

	iopen_close();

	enum go_file_failure_reason gffr;
	buffer_t *buffer = go_file(top_working_directory(), path, false, false, &gffr);

	if (buffer != NULL) {
		editor_t *editor = go_to_buffer(NULL, buffer, false);
		//printf("go_to_buffer <%p> <%s>\n", editor, search);
		if ((editor != NULL) && (search != NULL)) {
			//printf("searching for <%s>\n", search);
			editor->buffer->mark = editor->buffer->savedmark = -1;
			editor->buffer->cursor = 0;

			const char *argv[] = { "teddy_intl::iopen_search", search };
			interp_eval_command(editor, NULL, 2, argv);
			editor_include_cursor(editor, ICM_TOP, ICM_TOP);
			editor_grab_focus(editor, true);
		}
	} else {
		char *msg;
		asprintf(&msg, "Can not open%sfile %s", (gffr == GFFR_BINARYFILE ? " binary " : " "), path);
		alloc_assert(msg);
		quick_message("Error", msg);
		free(msg);
	}

	g_value_unset(&path_value);
	g_value_unset(&search_value);
}

static void iopen_enter(editor_t *editor) {
	GtkTreePath *path;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(results_tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	}
	iopen_open(GTK_TREE_VIEW(results_tree), path);
	gtk_tree_path_free(path);
}

static void result_activated_callback(GtkTreeView *tree, GtkTreePath *treepath, GtkTreeViewColumn *column, gpointer data) {
	iopen_open(tree, treepath);
}

static bool iopen_other_keys(editor_t *editor, bool shift, bool ctrl, bool alt, bool super, guint keyval) {
	if (!shift && !ctrl && !alt && !super) {
		GtkTreePath *path;

		switch (keyval) {
		case GDK_KEY_Up:
			gtk_tree_view_get_cursor(GTK_TREE_VIEW(results_tree), &path, NULL);
			if (path == NULL) {
				path = gtk_tree_path_new_first();
			} else {
				gtk_tree_path_prev(path);
			}
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(results_tree), path, NULL, FALSE);
			gtk_tree_path_free(path);
			return true;
		case GDK_KEY_Down:
			gtk_tree_view_get_cursor(GTK_TREE_VIEW(results_tree), &path, NULL);
			if (path == NULL) {
				path = gtk_tree_path_new_first();
			} else {
				gtk_tree_path_next(path);
			}
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(results_tree), path, NULL, FALSE);
			gtk_tree_path_free(path);
			return true;
		}
	}
	return false;
}

static void iopen_result_free(struct iopen_result *r) {
	free(r->path);
	g_free(r->show);
	if (r->search != NULL) free(r->search);
	free(r);
}

static gboolean iopen_add_result(struct iopen_result *r) {
	bool should_add = true;
	GtkTreeIter mah;

	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(results_list), &mah)) {
		do {
			GValue path_value = { 0 }, show_value = { 0 };
			gtk_tree_model_get_value(GTK_TREE_MODEL(results_list), &mah, 1, &path_value);
			gtk_tree_model_get_value(GTK_TREE_MODEL(results_list), &mah, 0, &show_value);
			const char *path = g_value_get_string(&path_value);
			const char *show = g_value_get_string(&show_value);

			if ((null_strcmp(r->show, show) == 0) && (null_strcmp(r->path, path) == 0)) should_add = false;

			g_value_unset(&path_value);
			g_value_unset(&show_value);
		} while (should_add && gtk_tree_model_iter_next(GTK_TREE_MODEL(results_list), &mah));
	}

	if (should_add) {
		gtk_list_store_append(results_list, &mah);
		gtk_list_store_set(results_list, &mah, 0, r->show, 1, r->path, 2, (r->search != NULL) ? r->search : "", 3, r->rank, -1);
	}

	iopen_result_free(r);

	return FALSE;
}

static void iopen_buffer_onchange(buffer_t *buffer) {
	g_async_queue_push(file_recursor_requests, buffer_all_lines_to_text(buffer));
	g_async_queue_push(tags_requests, buffer_all_lines_to_text(buffer));
	g_async_queue_push(buffers_requests, buffer_all_lines_to_text(buffer));
	gtk_list_store_clear(results_list);
	gtk_widget_queue_draw(results_tree);
}

static gpointer iopen_recursor_thread(gpointer data) {
	struct irt_frame {
		DIR *dir;
		struct dirent *cur;
	} tovisit[IOPEN_RECURSOR_DEPTH_LIMIT];
	int top = 0;
	char *request = NULL;

	int count = 0;
	char *rest = NULL;

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

			char *first_colon = strchr(request, ':');
			if (first_colon != NULL) {
				asprintf(&rest, "%s", first_colon);
				*first_colon = '\0';
			} else {
				if (rest != NULL) free(rest);
				rest = NULL;
			}

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
				alloc_assert(r->path);
				r->path[0] = '\0';
				for (int i = 0; i < top; ++i) {
					strcat(r->path, tovisit[i].cur->d_name);
					if (i != top-1) strcat(r->path, "/");
				}
				if (cf->cur->d_type == DT_DIR) strcat(r->path, "/");

				r->show = g_markup_printf_escaped("<big><b>%s%s</b></big>\n%s", cf->cur->d_name, (cf->cur->d_type == DT_DIR) ? "/" : "", r->path);
				alloc_assert(r->show);

				r->search = (rest != NULL) ? strdup(rest) : NULL;

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

static gpointer iopen_tags_thread(gpointer data) {
	char *request = NULL;
	int idx = -1;
	int count = 0;

	for (;;) {
		char *new_request;

		if (idx < 0) {
			//printf("Waiting\n");
			new_request = g_async_queue_pop(tags_requests);
		} else {
			new_request = g_async_queue_try_pop(tags_requests);
		}

		if (new_request != NULL) {
			// got a new request, discard the old one, reset the stack and start back at the top
			// an empty string as request will stop us if we were doing something

			//printf("New request: <%s>\n", new_request);

			if (request != NULL) free(request);
			request = new_request;

			char *first_colon = strchr(request, ':');
			if (first_colon != NULL) *first_colon = '\0';

			idx = (strcmp(request, "") == 0) ? -1 : 0;
			count = 0;
		}

		if (idx < 0) continue;

		if (idx >= tag_entries_cap) {
			idx = -1;
			count = 0;
			continue;
		}

		char *match_start = strcasestr(tag_entries[idx].tag, request);
		if (match_start != NULL) {
			struct iopen_result *r = malloc(sizeof(struct iopen_result));
			r->show = g_markup_printf_escaped("<big><b>%s</b></big>\n%s", tag_entries[idx].tag, tag_entries[idx].path);
			alloc_assert(r->show);
			r->path = strdup(tag_entries[idx].path);
			alloc_assert(r->path);
			if (tag_entries[idx].search != NULL) {
				r->search = strdup(tag_entries[idx].search);
				alloc_assert(r->search);
			} else {
				r->search = NULL;
			}
			r->rank = 1000 + (match_start - tag_entries[idx].tag) * 10 + strlen(tag_entries[idx].tag);
			g_idle_add((GSourceFunc)iopen_add_result, r);

			//printf("\t<%s>\n", tag_entries[idx].tag);

			if (count++ > IOPEN_MAX_SENT_RESULTS) {
				idx = -1;
				count = 0;
				continue;
			}
		}

		++idx;
	}

	return NULL;
}

// Returns 0 if no indentation, 1 if there is only one tab or at most 4 spaces, -1 otherwise
static int too_much_indent(const char *line) {
	if (line[0] == '\t') {
		if (line[1] == ' ') return -1;
		if (line[1] == '\t') return -1;
		return 1;
	} else if (line[0] == ' ') {
		for (int i = 1; line[i] != '\0'; ++i) {
			if (i > 4) return -1;
			if (line[i] != ' ') return 1;
		}
		return 1;
	} else {
		return 0;
	}
}

static gpointer iopen_buffer_thread(gpointer data) {
	char *request = NULL;
	uint32_t *urequest = NULL;
	int ureqlen = 0;
	int bufidx = -1;
	int count;

	for (;;) {
		char *new_request;

		if (bufidx < 0) {
			//printf("Waiting\n");
			new_request = g_async_queue_pop(buffers_requests);
		} else {
			new_request = g_async_queue_try_pop(buffers_requests);
		}

		if (new_request != NULL) {
			if (request != NULL) {
				free(request);
				free(urequest);
			}
			if (strlen(new_request) >= 3) {
				request = new_request;
			} else {
				request = strdup("");
			}

			char *first_colon = strchr(request, ':');
			if (first_colon != NULL) *first_colon = '\0';

			bufidx = (strcmp(request, "") == 0) ? -1 : 0;
			count = 0;

			urequest = utf8_to_utf32_string(request, &ureqlen);
		}

		if (bufidx < 0) continue;

		buffer_t *buf = NULL;

		for (; bufidx < buffers_allocated; ++bufidx) {
			if ((buffers[bufidx] != NULL) && (buffers[bufidx]->path[0] != '+') && (buffers[bufidx]->path[strlen(buffers[bufidx]->path)-1] != '/')) {
				buf = buffers[bufidx];
				break;
			}
		}

		if (buf == NULL) {
			bufidx = -1;
			count = 0;
			continue;
		}

		for (int p = 0; p+ureqlen < MIN(100000, BSIZE(buf)); ++p) {
			int i;
			for (i = 0; i < ureqlen; ++i) {
				my_glyph_info_t *g = bat(buf, p+i);
				if (urequest[i] != g->code) break;
			}
			if (i == ureqlen) {
				// match found
				int s = p, e = p;
				buffer_move_point_glyph(buf, &s, MT_ABS, 1);
				buffer_move_point_glyph(buf, &e, MT_END, 0);

				char *line = buffer_lines_to_text(buf, s, e);

				int indentation_depth = too_much_indent(line);
				if (indentation_depth >= 0) {
					int lineno = buffer_line_of(buf, p, false);
					struct iopen_result *r = malloc(sizeof(struct iopen_result));

					int c = strncmp(buf->path, top_working_directory(), strlen(top_working_directory()));
					if (c == 0) {
						r->path = strdup(buf->path + strlen(top_working_directory()) + 1);
					} else {
						r->path = strdup(buf->path);
					}
					alloc_assert(r->path);

					r->show = g_markup_printf_escaped("<big><b>%s</b></big>\n%s:%d", line, r->path, lineno);
					free(line);

					asprintf(&(r->search), ":%d", lineno);
					alloc_assert(r->search);
					r->rank = 10000 + indentation_depth * 100 + (p - s);
					g_idle_add((GSourceFunc)iopen_add_result, r);

					//printf("\t<%s>\n", tag_entries[idx].tag);

					if (count++ > IOPEN_MAX_SENT_RESULTS) {
						bufidx = -1;
						count = 0;
						break;
					}
				} else {
					free(line);
				}

				p = e+1;
			}
		}

		if (count++ > IOPEN_MAX_SENT_RESULTS) {
			bufidx = -1;
			count = 0;
			continue;
		}

		++bufidx;
	}

	return NULL;
}

static gboolean map_callback(GtkWidget *widget, GdkEvent *event, gpointer d) {
	editor_grab_focus(iopen_editor, false);
	return FALSE;
}

void iopen_init(GtkWidget *window) {
	parent_window = window;

	GtkWidget *main_vbox = gtk_vbox_new(false, 0);

	iopen_buffer = buffer_create();
	iopen_buffer->single_line = true;
	load_empty(iopen_buffer);
	iopen_editor = new_editor(iopen_buffer, true);

	buffer_set_onchange(iopen_buffer, iopen_buffer_onchange);

	config_set(&(iopen_buffer->config), CFG_AUTOWRAP, "0");
	config_set(&(iopen_buffer->config), CFG_AUTOCOMPL_POPUP, "0");

	iopen_editor->single_line_escape = &iopen_escape;
	iopen_editor->single_line_return = &iopen_enter;
	iopen_editor->single_line_other_keys = &iopen_other_keys;

	gtk_box_pack_start(GTK_BOX(main_vbox), GTK_WIDGET(iopen_editor), FALSE, FALSE, 0);

	results_list = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(results_list), 3, GTK_SORT_ASCENDING);
	results_tree = gtk_tree_view_new();

	crt = gtk_cell_renderer_text_new();

	iopen_recoloring();

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(results_tree), -1, "Show", crt, "markup", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(results_tree), GTK_TREE_MODEL(results_list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(results_tree), FALSE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(results_tree), 1);

	g_signal_connect(G_OBJECT(results_tree), "row-activated", G_CALLBACK(result_activated_callback), NULL);

	GtkWidget *results_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(results_scroll), results_tree);

	gtk_box_pack_start(GTK_BOX(main_vbox), frame_piece(TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox),  results_scroll, TRUE, TRUE, 0);

	iopen_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(iopen_window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(iopen_window), 640, 480);

	g_signal_connect(G_OBJECT(iopen_window), "delete-event", G_CALLBACK(delete_callback), NULL);
	g_signal_connect(G_OBJECT(iopen_window), "map_event", G_CALLBACK(map_callback), NULL);

	gtk_container_add(GTK_CONTAINER(iopen_window), main_vbox);

	file_recursor_requests = g_async_queue_new();
	tags_requests = g_async_queue_new();
	buffers_requests = g_async_queue_new();
	g_thread_new("iopen file recursion", iopen_recursor_thread, NULL);
	g_thread_new("tags iteration", iopen_tags_thread, NULL);
	g_thread_new("buffer iteration", iopen_buffer_thread, NULL);
}

void iopen(const char *initial_text) {
	if (interp_context_buffer() == iopen_buffer) {
		iopen_enter(iopen_editor);
		return;
	}

	gtk_window_set_transient_for(GTK_WINDOW(iopen_window), GTK_WINDOW(parent_window));
	gtk_window_set_modal(GTK_WINDOW(iopen_window), TRUE);
	//gtk_window_set_position(GTK_WINDOW(iopen_window), GTK_WIN_POS_CENTER_ON_PARENT);

	gtk_list_store_clear(results_list);

	buffer_aux_clear(iopen_buffer);
	iopen_buffer->cursor = 0;

	if (initial_text != NULL) {
		buffer_replace_selection(iopen_buffer, initial_text);
		iopen_buffer->mark = 0;
	}

	gtk_widget_show_all(iopen_window);
	editor_grab_focus(iopen_editor, false);
}

void iopen_recoloring(void) {
	GdkColor fg;
	set_gdk_color_cfg(&global_config, CFG_EDITOR_FG_COLOR, &fg);
	g_object_set(crt, "foreground-gdk", &fg, "foreground-set", TRUE, NULL);

	gtk_widget_like_editor(&global_config, results_tree);
}
