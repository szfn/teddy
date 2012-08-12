#include "top.h"

#include <unistd.h>

#include <gdk/gdkkeysyms.h>

#include "interp.h"
#include "history.h"
#include "global.h"
#include "baux.h"

GtkWidget *top_notebook;
buffer_t *cmdline_buffer;
editor_t *cmdline_editor;
editor_t *the_top_context_editor;
char *working_directory;

static void execute_command(editor_t *editor) {
	lpoint_t start, end;
	buffer_get_extremes(editor->buffer, &start, &end);

	char *command = buffer_lines_to_text(editor->buffer, &start, &end);

	enum deferred_action da = interp_eval(top_context_editor(), command, false);
	history_add(&command_history, time(NULL), working_directory, command, true);

	switch(da) {
	case FOCUS_ALREADY_SWITCHED:
		break;
	case CLOSE_EDITOR:
		editor_close_editor(editor);
		break;
	case NOTHING:
	default:
		// does not switch focus back to the editor by default
		break;
	}

	free(command);

	copy_lpoint(&(editor->buffer->mark), &start);
	copy_lpoint(&(editor->buffer->cursor), &end);
	editor_replace_selection(editor, "");
}

static void release_command_line(editor_t *editor) {
	if (top_context_editor() != NULL) {
		editor_grab_focus(top_context_editor(), false);
	} else {
		gtk_widget_grab_focus(GTK_WIDGET(columnset));
	}
}

static void history_substitute_with_index(struct history *h, editor_t *editor) {
	char *r = history_index_get(h);
	lpoint_t start, end;
	buffer_get_extremes(editor->buffer, &start, &end);

	copy_lpoint(&(editor->buffer->mark), &start);
	copy_lpoint(&(editor->buffer->cursor), &end);
	editor_replace_selection(editor, r);
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
			editor_absolute_cursor_position(editor, &x, &y, &alty);

			char *buffer_text = buffer_all_lines_to_text(editor->buffer);

			COMPL_WND_HIDE(&the_generic_cmd_completer);
			compl_wnd_show(&(command_history.c), buffer_text, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)), true, true);

			free(buffer_text);

			return true;
		}
	}
	history_index_reset(&command_history);
	return false;
}

GtkWidget *top_init(void) {
	top_notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(top_notebook), FALSE);

	cmdline_buffer = buffer_create();
	load_empty(cmdline_buffer);
	cmdline_editor = new_editor(cmdline_buffer, true);

	config_set(&(cmdline_buffer->config), CFG_AUTOWRAP, "0");

	gtk_notebook_append_page(GTK_NOTEBOOK(top_notebook), GTK_WIDGET(cmdline_editor), NULL);

	the_top_context_editor = NULL;

	cmdline_editor->single_line_escape = &release_command_line;
	cmdline_editor->single_line_return = &execute_command;
	cmdline_editor->single_line_other_keys = &cmdline_other_keys;
	cmdline_editor->completer = &the_generic_cmd_completer;

	working_directory = get_current_dir_name();

	return top_notebook;
}

void top_start_command_line(editor_t *editor) {
	the_top_context_editor = editor;
	editor_grab_focus(cmdline_editor, false);
}

editor_t *top_context_editor(void) {
	return the_top_context_editor;
}

char *top_working_directory(void) {
	return working_directory;
}
