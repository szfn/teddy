#include "top.h"

#include <unistd.h>

#include <gdk/gdkkeysyms.h>

#include "interp.h"
#include "history.h"
#include "global.h"
#include "tags.h"
#include "buffers.h"
#include "iopen.h"
#include "git.date.h"

GtkWidget *top_window;
buffer_t *cmdline_buffer;
editor_t *cmdline_editor;
editor_t *the_top_context_editor;

GtkWidget *tools_menu;

char *working_directory;

GdkPixbuf *tools_pixbuf = NULL;
static const guint8 cog_icon[] __attribute__ ((__aligned__ (4))) =
{ ""
  /* Pixbuf magic (0x47646b50) */
  "GdkP"
  /* length: header (24) + pixel_data (1024) */
  "\0\0\4\30"
  /* pixdata_type (0x1010002) */
  "\1\1\0\2"
  /* rowstride (64) */
  "\0\0\0@"
  /* width (16) */
  "\0\0\0\20"
  /* height (16) */
  "\0\0\0\20"
  /* pixel_data: */
  "\0\0\0\0\0\0\0\0\0\0\0\0""111\20""111\277222\377222\200\0\0\0\0\0\0\0"
  "\0""222\200222\377111\277111\20\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0///@///\377///\377///\377---@---@///\377///\377///\377///@"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0,,,\377,,,\377"
  ",,,\377,,,\377,,,\377,,,\377,,,\377,,,\377\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0'''\20'''@\0\0\0\0'''0(((\377(((\377(((\337)))\217)))\217(((\337"
  "(((\377(((\377'''0\0\0\0\0'''@'''\20%%%\277%%%\377%%%\377%%%\377%%%\377"
  "&&&p\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0&&&p%%%\377%%%\377%%%\377%%%\377"
  "%%%\277\"\"\"\377\"\"\"\377\"\"\"\377\"\"\"\377\"\"\"p\0\0\0\0!!!\40"
  "!!!\200!!!\200!!!\40\0\0\0\0\"\"\"p\"\"\"\377\"\"\"\377\"\"\"\377\"\""
  "\"\377\37\37\37\200\37\37\37\377\37\37\37\377\37\37\37\337\0\0\0\0\36"
  "\36\36\40\36\36\36\357\37\37\37\377\37\37\37\377\36\36\36\357\36\36\36"
  "\40\0\0\0\0\37\37\37\337\37\37\37\377\37\37\37\377\37\37\37\200\0\0\0"
  "\0\34\34\34@\34\34\34\377\34\34\34\277\0\0\0\0\34\34\34\200\34\34\34"
  "\377\35\35\35""0\35\35\35""0\34\34\34\377\34\34\34\200\0\0\0\0\34\34"
  "\34\277\34\34\34\377\34\34\34@\0\0\0\0\0\0\0\0\30\30\30@\31\31\31\377"
  "\31\31\31\277\0\0\0\0\31\31\31p\31\31\31\377\27\27\27""0\27\27\27""0"
  "\31\31\31\377\31\31\31p\0\0\0\0\31\31\31\277\31\31\31\377\30\30\30@\0"
  "\0\0\0\25\25\25\200\25\25\25\377\25\25\25\377\25\25\25\357\24\24\24\20"
  "\26\26\26\20\25\25\25\357\25\25\25\377\25\25\25\377\25\25\25\357\26\26"
  "\26\20\24\24\24\20\25\25\25\357\25\25\25\377\25\25\25\377\25\25\25\200"
  "\22\22\22\377\22\22\22\377\22\22\22\377\22\22\22\377\22\22\22\217\0\0"
  "\0\0\23\23\23\20\23\23\23p\23\23\23`\23\23\23\20\0\0\0\0\22\22\22\217"
  "\22\22\22\377\22\22\22\377\22\22\22\377\22\22\22\377\17\17\17\277\17"
  "\17\17\377\17\17\17\377\17\17\17\377\17\17\17\377\16\16\16\217\16\16"
  "\16\20\0\0\0\0\0\0\0\0\16\16\16\20\16\16\16\217\17\17\17\377\17\17\17"
  "\377\17\17\17\377\17\17\17\377\17\17\17\277\15\15\15\20\15\15\15@\0\0"
  "\0\0\15\15\15""0\14\14\14\377\14\14\14\377\14\14\14\377\13\13\13\277"
  "\13\13\13\277\14\14\14\377\14\14\14\377\14\14\14\377\15\15\15""0\0\0"
  "\0\0\15\15\15@\15\15\15\20\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\11\11\11\377"
  "\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11\377\11\11\11\377\11"
  "\11\11\377\11\11\11\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\5\5\5@\5\5\5\377\5\5\5\377\5\5\5\377\6\6\6@\6\6\6@\5\5\5\377"
  "\5\5\5\377\5\5\5\377\5\5\5@\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\3\3\3\20\2\2\2\277\2\2\2\377\2\2\2\200\0\0\0\0\0\0\0\0\2\2\2\200"
  "\2\2\2\377\2\2\2\277\3\3\3\20\0\0\0\0\0\0\0\0\0\0\0\0"};


static void execute_command(editor_t *editor) {
	char *command = buffer_all_lines_to_text(editor->buffer);

	interp_eval(top_context_editor(), NULL, command, false);

	if (top_context_editor() != NULL) {
		set_label_text(top_context_editor());
		gtk_widget_queue_draw(GTK_WIDGET(top_context_editor()));
	}

	// select contents of command line
	buffer_get_extremes(editor->buffer, &(editor->buffer->mark), &(editor->buffer->cursor));
	gtk_widget_queue_draw(GTK_WIDGET(editor));

	history_add(&command_history, time(NULL), working_directory, command, true);

	free(command);
}

static void release_command_line(editor_t *editor) {
	if (top_context_editor() != NULL) {
		if (config_intval(&global_config, CFG_FOCUS_FOLLOWS_MOUSE)) {
			int x, y;
			gtk_widget_get_pointer(GTK_WIDGET(columnset), &x, &y);
			GList *cols = gtk_container_get_children(GTK_CONTAINER(columnset));
			for (GList *col = cols; col != NULL; col = col->next) {
				GList *frames = gtk_container_get_children(GTK_CONTAINER(col->data));
				for (GList *frame = frames; frame != NULL; frame = frame->next) {
					GtkAllocation allocation;
					gtk_widget_get_allocation(frame->data, &allocation);

					if (inside_allocation(x, y, &allocation)) {
						tframe_t *f = GTK_TFRAME(frame->data);
						if (GTK_IS_TEDITOR(tframe_content(f))) {
							editor_complete_move(GTK_TEDITOR(tframe_content(f)), true);
							editor_grab_focus(GTK_TEDITOR(tframe_content(f)), false);
						}
					}
				}
				g_list_free(frames);
			}
			g_list_free(cols);
		} else {
			editor_complete_move(top_context_editor(), true);
			editor_grab_focus(top_context_editor(), false);
		}
	} else {
		gtk_widget_grab_focus(GTK_WIDGET(columnset));
	}
}

static void history_substitute_with_index(struct history *h, editor_t *editor) {
	char *r = history_index_get(h);
	if (r == NULL) return;
	buffer_get_extremes(editor->buffer, &(editor->buffer->mark), &(editor->buffer->cursor));

	buffer_replace_selection(editor->buffer, r);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

bool cmdline_other_keys(struct _editor_t *editor, bool shift, bool ctrl, bool alt, bool super, guint keyval) {
	if (!shift && !ctrl && !alt && !super) {
		switch (keyval) {
		case GDK_KEY_Up:
			history_index_next(&command_history);
			history_substitute_with_index(&command_history, editor);
			return true;
		case GDK_KEY_Down:
			history_index_prev(&command_history);
			history_substitute_with_index(&command_history, editor);
			return true;
		}
	} else if (!shift && ctrl && !alt && !super) {
		if (keyval == GDK_KEY_r) {
			double x, y, alty;
			editor_cursor_position(editor, &x, &y, &alty);

			char *buffer_text = buffer_all_lines_to_text(editor->buffer);

			compl_wnd_hide(&the_word_completer);
			compl_wnd_show(&(command_history.c), buffer_text, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)), true, true);

			free(buffer_text);

			return true;
		}
	}
	history_index_reset(&command_history);
	return false;
}

static gboolean tools_label_map_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
	gdk_window_set_cursor(gtk_widget_get_window(widget), cursor_hand);
	return false;
}

static void tools_menu_position_function(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, GtkWidget *widget) {
	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	GtkAllocation menu_allocation;
	gtk_widget_get_allocation(GTK_WIDGET(menu), &menu_allocation);

	gint wpos_x, wpos_y;
	gdk_window_get_position(gtk_widget_get_window(top_window), &wpos_x, &wpos_y);

	*x = wpos_x + allocation.x + allocation.width - menu_allocation.width;
	*y = wpos_y + allocation.y + allocation.height;
}

static gboolean tools_label_popup_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	if (event->type != GDK_BUTTON_PRESS) return FALSE;
	if (event->button != 1) return FALSE;

	gtk_menu_popup(GTK_MENU(tools_menu), NULL, NULL, (GtkMenuPositionFunc)tools_menu_position_function, widget, 0, event->time);

	return FALSE;
}

static void new_column_mitem_callback(GtkMenuItem *menuitem, gpointer data) {
	column_t *col = column_new(0);
	columns_append(columnset, col, true);
	heuristic_new_frame(columnset, NULL, null_buffer());
}

static void close_mitem_callback(GtkMenuItem *menuitem, gpointer data) {
	if (!buffers_close_all()) return;
	gtk_widget_destroy(top_window);
}

static void iopen_mitem_callback(GtkMenuItem *menuitem, gpointer data) {
	iopen();
}

static void open_mitem_callback(GtkMenuItem *menuitem, gpointer data) {
	buffer_get_extremes(cmdline_editor->buffer, &(cmdline_buffer->mark), &(cmdline_buffer->cursor));
	buffer_replace_selection(cmdline_editor->buffer, "O ");
	editor_grab_focus(cmdline_editor, false);
}

static void save_session_mitem_callback(GtkMenuItem *menuitem, gpointer data) {
	interp_eval(NULL, NULL, "teddy_intl::savesession_mitem", false);
}

static void load_session_mitem_callback(GtkMenuItem *menuitem, gpointer data) {
	interp_eval(NULL, NULL, "teddy_intl::loadsession_mitem", false);
}

GtkWidget *top_init(GtkWidget *window) {
	top_window = window;

	/**** COMMAND LINE ****/

	cmdline_buffer = buffer_create();
	cmdline_buffer->single_line = true;
	load_empty(cmdline_buffer);
	cmdline_editor = new_editor(cmdline_buffer, true);

	config_set(&(cmdline_buffer->config), CFG_AUTOWRAP, "0");
	config_set(&(cmdline_buffer->config), CFG_AUTOCOMPL_POPUP, "0");

	the_top_context_editor = NULL;

	cmdline_editor->single_line_escape = &release_command_line;
	cmdline_editor->single_line_return = &execute_command;
	cmdline_editor->single_line_other_keys = &cmdline_other_keys;
	cmdline_editor->alt_completer = &(command_history.c);

	working_directory = get_current_dir_name();
	tags_load(working_directory);

	GtkWidget *top_box = gtk_hbox_new(false, 0);

	if (tools_pixbuf == NULL) tools_pixbuf = gdk_pixbuf_new_from_inline(sizeof(cog_icon), cog_icon, FALSE, NULL);

	GtkWidget *tools_box = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(tools_box), gtk_image_new_from_pixbuf(tools_pixbuf));
	gtk_widget_like_editor(&global_config, tools_box);

	gtk_box_pack_start(GTK_BOX(top_box), tools_box, FALSE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(top_box), GTK_WIDGET(cmdline_editor), TRUE, TRUE, 0);

	gtk_widget_add_events(tools_box, GDK_STRUCTURE_MASK);

	g_signal_connect(G_OBJECT(tools_box), "map_event", G_CALLBACK(tools_label_map_callback), NULL);
	g_signal_connect(G_OBJECT(tools_box), "button_press_event", G_CALLBACK(tools_label_popup_callback), NULL);


	/**** TOOLS MENU ****/

	tools_menu = gtk_menu_new();

	GtkWidget *iopen_mitem = gtk_menu_item_new_with_label("Fuzzy search file");
	g_signal_connect(G_OBJECT(iopen_mitem), "activate", G_CALLBACK(iopen_mitem_callback), NULL);
	gtk_menu_append(tools_menu, iopen_mitem);

	GtkWidget *open_mitem = gtk_menu_item_new_with_label("Open file");
	g_signal_connect(G_OBJECT(open_mitem), "activate", G_CALLBACK(open_mitem_callback), NULL);
	gtk_menu_append(tools_menu, open_mitem);

	GtkWidget *new_column_mitem = gtk_menu_item_new_with_label("New column");
	g_signal_connect(G_OBJECT(new_column_mitem), "activate", G_CALLBACK(new_column_mitem_callback), NULL);
	gtk_menu_append(tools_menu, new_column_mitem);

	gtk_menu_append(tools_menu, gtk_separator_menu_item_new());

	GtkWidget *load_session_mitem = gtk_menu_item_new_with_label("Load session");
	g_signal_connect(G_OBJECT(load_session_mitem), "activate", G_CALLBACK(load_session_mitem_callback), NULL);
	gtk_menu_append(tools_menu, load_session_mitem);

	GtkWidget *save_session_mitem = gtk_menu_item_new_with_label("Save session");
	g_signal_connect(G_OBJECT(save_session_mitem), "activate", G_CALLBACK(save_session_mitem_callback), NULL);
	gtk_menu_append(tools_menu, save_session_mitem);

	gtk_menu_append(tools_menu, gtk_separator_menu_item_new());

	GtkWidget *close_mitem = gtk_menu_item_new_with_label("Quit");
	g_signal_connect(G_OBJECT(close_mitem), "activate", G_CALLBACK(close_mitem_callback), NULL);
	gtk_menu_append(tools_menu, close_mitem);

	gtk_widget_show_all(tools_menu);

	/**** END ****/
	top_cd(".");

	GtkWidget *boxcoloring = gtk_event_box_new();
	gtk_widget_like_editor(&global_config, boxcoloring);
	gtk_container_add(GTK_CONTAINER(boxcoloring), top_box);

	return boxcoloring;
}

void top_start_command_line(editor_t *editor, const char *text) {
	the_top_context_editor = editor;
	buffer_get_extremes(cmdline_editor->buffer, &(cmdline_editor->buffer->mark), &(cmdline_editor->buffer->cursor));
	if (text != NULL) {
		buffer_replace_selection(cmdline_editor->buffer, " {");
		buffer_replace_selection(cmdline_editor->buffer, text);
		buffer_replace_selection(cmdline_editor->buffer, "}");
		cmdline_editor->buffer->cursor = 0;
	}
	editor_grab_focus(cmdline_editor, false);
}

editor_t *top_context_editor(void) {
	return the_top_context_editor;
}

void top_context_editor_gone(void) {
	the_top_context_editor = NULL;
}

char *top_working_directory(void) {
	return working_directory;
}

void top_cd(const char *newdir) {
	free(working_directory);

	if (newdir[0] == '~') {
		char *t;
		asprintf(&t, "%s%s", getenv("HOME"), newdir+1);
		alloc_assert(t);
		chdir(t);
		free(t);
	} else {
		chdir(newdir);
	}

	working_directory = get_current_dir_name();

	char *t;

	if (tied_session == NULL) {
		if (strncmp(working_directory, getenv("HOME"), strlen(getenv("HOME"))) == 0) {
			asprintf(&t, "%s – ~%s", GIT_COMPILATION_DATE, working_directory+strlen(getenv("HOME")));
		} else {
			asprintf(&t, "%s – %s", GIT_COMPILATION_DATE, working_directory);
		}
	} else {
		asprintf(&t, "%s @ %s", GIT_COMPILATION_DATE, tied_session);
	}

	alloc_assert(t);
	gtk_window_set_title(GTK_WINDOW(top_window), t);
	free(t);
	tags_load(working_directory);
}

bool top_command_line_focused(void) {
	return gtk_widget_is_focus(cmdline_editor->drar);
}

bool top_has_tags(void) {
	return tags_loaded();
}
