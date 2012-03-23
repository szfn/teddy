#include "compl.h"

#include "global.h"

static gboolean compl_wnd_expose_callback(GtkWidget *widget, GdkEventExpose *event, struct completer *c) {
	GtkAllocation allocation;

	gtk_widget_get_allocation(c->window, &allocation);

	gint wpos_x, wpos_y;
	gdk_window_get_position(gtk_widget_get_window(c->window), &wpos_x, &wpos_y);

	double x = wpos_x + allocation.width, y = wpos_y + allocation.height;

	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *screen = gdk_display_get_default_screen(display);

	gint screen_width = gdk_screen_get_width(screen);
	gint screen_height = gdk_screen_get_height(screen);

	//printf("bottom right corner (%g, %g) screen width: %d height: %d\n", x, y, screen_width, screen_height);

	if (x > screen_width) {
		gtk_window_move(GTK_WINDOW(c->window), allocation.x + screen_width - x - 5, allocation.y);
	}

	if (y > screen_height) {
		gtk_window_move(GTK_WINDOW(c->window), allocation.x, c->alty - allocation.height);
	}

	return FALSE;
}

void compl_init(struct completer *c) {
	c->cbt.root = NULL;
	c->list = gtk_list_store_new(1, G_TYPE_STRING);
	c->tree = gtk_tree_view_new();

	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(c->tree), -1, "Completion", gtk_cell_renderer_text_new(), "text", 0, NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(c->tree), GTK_TREE_MODEL(c->list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(c->tree), FALSE);

	c->window = gtk_window_new(GTK_WINDOW_POPUP);

	gtk_window_set_decorated(GTK_WINDOW(c->window), FALSE);

	g_signal_connect(G_OBJECT(c->window), "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(G_OBJECT(c->tree), "expose_event", G_CALLBACK(compl_wnd_expose_callback), c);

	{
		GtkWidget *frame = gtk_table_new(0, 0, FALSE);

		gtk_container_add(GTK_CONTAINER(c->window), frame);

		place_frame_piece(frame, TRUE, 0, 3); // top frame
		place_frame_piece(frame, FALSE, 0, 3); // left frame
		place_frame_piece(frame, FALSE, 2, 3); // right frame
		place_frame_piece(frame, TRUE, 2, 3); // bottom frame

		GtkWidget *scroll_view = gtk_scrolled_window_new(NULL, NULL);

		gtk_container_add(GTK_CONTAINER(scroll_view), c->tree);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_view), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		gtk_table_attach(GTK_TABLE(frame), scroll_view, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	}

	gtk_window_set_default_size(GTK_WINDOW(c->window), -1, 150);
}

void compl_reset(struct completer *c) {
	critbit0_clear(&(c->cbt));
}

void compl_add(struct completer *c, const char *text) {
	critbit0_insert(&(c->cbt), text);
}

char *compl_complete(struct completer *c, const char *prefix) {
	char *r = critbit0_common_suffix_for_prefix(&(c->cbt), prefix);
	utf8_remove_truncated_characters_at_end(r);
	return r;
}

void compl_add_to_list(struct completer *c, const char *text) {
	GtkTreeIter mah;
	gtk_list_store_append(c->list, &mah);
	gtk_list_store_set(c->list, &mah, 0, text, -1);
	++(c->size);
}

static int compl_wnd_fill_callback(const char *entry, void *p) {
	struct completer *c = (struct completer *)p;
	compl_add_to_list(c, entry);
	return 1;
}

void compl_wnd_show(struct completer *c, const char *prefix, double x, double y, double alty, GtkWidget *parent, bool show_empty, bool show_empty_prefix) {
	c->size = 0;
	c->alty = alty;

	if (!show_empty_prefix) {
		if (strcmp(prefix, "") == 0) {
			compl_wnd_hide(c);
			return;
		}
	}

	gtk_list_store_clear(c->list);
	critbit0_allprefixed(&(c->cbt), prefix, compl_wnd_fill_callback, (void *)c);

	if (!show_empty) {
		if (c->size == 0) {
			compl_wnd_hide(c);
			return;
		}
	}

	gtk_window_set_transient_for(GTK_WINDOW(c->window), GTK_WINDOW(parent));

	x += 2; y += 2;

	gtk_widget_set_uposition(c->window, x, y);

	{
		GtkTreePath *path_to_first = gtk_tree_path_new_first();
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path_to_first, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);
		gtk_tree_path_free(path_to_first);
	}

	gtk_widget_show_all(c->window);
	c->visible = true;
	c->prefix_len = strlen(prefix);
}

void compl_wnd_up(struct completer *c) {
	GtkTreePath *path;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	} else {
		if (!gtk_tree_path_prev(path)) {
			gtk_tree_path_free(path);
			path = gtk_tree_path_new_from_indices(c->size-1, -1);
		}
	}

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);
	gtk_tree_path_free(path);
}

void compl_wnd_down(struct completer *c) {
	GtkTreePath *path;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &path, NULL);

	if (path == NULL) {
		path = gtk_tree_path_new_first();
	} else {
		gtk_tree_path_next(path);
	}

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);

	gtk_tree_path_free(path);

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &path, NULL);
	if (path == NULL) {
		path = gtk_tree_path_new_first();
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(c->tree), path, gtk_tree_view_get_column(GTK_TREE_VIEW(c->tree), 0), FALSE);
	}

	gtk_tree_path_free(path);
}

char *compl_wnd_get(struct completer *c, bool all) {
	GtkTreePath *focus_path;
	GtkTreeIter iter;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(c->tree), &focus_path, NULL);
	if (focus_path == NULL) {
		gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(c->list), &iter);
		if (!valid) return NULL;
	} else {
		gboolean valid = gtk_tree_model_get_iter(GTK_TREE_MODEL(c->list), &iter, focus_path);
		if (!valid) return NULL;
	}

	GValue value = {0};
	gtk_tree_model_get_value(GTK_TREE_MODEL(c->list), &iter, 0, &value);
	const char *pick = g_value_get_string(&value);
	if (focus_path != NULL) {
		gtk_tree_path_free(focus_path);
	}

	if (c->prefix_len >= strlen(pick)) {
		return NULL;
	}

	char *r = strdup(all ? pick : (pick+c->prefix_len));
	alloc_assert(r);

	g_value_unset(&value);

	return r;
}

void compl_wnd_hide(struct completer *c) {
	if (!(c->visible)) return;
	gtk_widget_hide(c->window);
	c->visible = false;
}

bool compl_wnd_visible(struct completer *c) {
	return c->visible;
}

void compl_free(struct completer *c) {
	critbit0_clear(&(c->cbt));
}
