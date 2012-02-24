#include "editor_cmdline.h"

#include <gdk/gdkkeysyms.h>
#include <unicode/uchar.h>
#include <stdbool.h>

#include "global.h"
#include "cmdcompl.h"
#include "interp.h"
#include "cfg.h"
#include "lexy.h"

static bool should_be_case_sensitive(uint32_t *needle, int len) {
	if (config[CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE].intval == 0) return false;
	if (config[CFG_INTERACTIVE_SEARCH_CASE_SENSITIVE].intval == 1) return true;

	// smart case sensitiveness set up here

	for (int i = 0; i < len; ++i) {
		if (u_isupper(needle[i])) return true;
	}

	return false;
}

typedef bool uchar_match_fn(uint32_t a, uint32_t b);

bool uchar_match_case_sensitive(uint32_t a, uint32_t b) {
	return a == b;
}

bool uchar_match_case_insensitive(uint32_t a, uint32_t b) {
	return u_tolower(a) == u_tolower(b);
}

static void move_search_forward(editor_t *editor, gboolean ctrl_g_invoked) {
	const gchar *text = gtk_entry_get_text(GTK_ENTRY(editor->entry));
	int len = strlen(text);

	uint32_t *needle = malloc(len*sizeof(uint32_t));

	real_line_t *search_line;
	int search_glyph;

	int dst, i;

	for (i = 0, dst = 0; i < len; ) {
		bool valid = true;
		needle[dst++] = utf8_to_utf32(text, &i, len, &valid);
	}

	bool case_sensitive = should_be_case_sensitive(needle, dst);
	uchar_match_fn *match_fn = case_sensitive ? &uchar_match_case_sensitive : &uchar_match_case_insensitive;

	/*
	printf("Searching [");
	for (i = 0; i < dst; ++i) {
		printf("%d ", needle[i]);
	}
	printf("]\n");*/

	if ((editor->buffer->mark.line == NULL) || editor->search_failed) {
		search_line = editor->buffer->real_line;
		search_glyph = 0;
	} else if (ctrl_g_invoked) {
		search_line = editor->buffer->cursor.line;
		search_glyph = editor->buffer->cursor.glyph;
	} else {
		search_line = editor->buffer->mark.line;
		search_glyph = editor->buffer->mark.glyph;
	}

	for ( ; search_line != NULL; search_line = search_line->next) {
		int i = search_glyph, j = 0;
		search_glyph = 0; // every line searched after the first line will start from the beginning

		for ( ; i < search_line->cap; ++i) {
			if (j >= dst) break;
			if (match_fn(search_line->glyph_info[i].code, needle[j])) {
				++j;
			} else {
				i -= j;
				j = 0;
			}
		}

		if (j >= dst) {
			// search was successful
			editor->buffer->mark.line = search_line;
			editor->buffer->mark.glyph = i - j;
			editor->buffer->cursor.line = search_line;
			editor->buffer->cursor.glyph = i;
			lexy_update_for_move(editor->buffer, editor->buffer->cursor.line);
			break;
		}
	}

	if (search_line == NULL) {
		editor->search_failed = 0;
		buffer_unset_mark(editor->buffer);
	}

	free(needle);

	editor_center_on_cursor(editor);
	gtk_widget_queue_draw(editor->drar);
}

static gboolean entry_search_insert_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	editor_t *editor = (editor_t*)data;
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	if (editor->ignore_next_entry_keyrelease) {
		editor->ignore_next_entry_keyrelease = 0;
		return TRUE;
	}

	gboolean ctrl_g_invoked = FALSE;

	if ((event->keyval == GDK_KEY_Escape) || (event->keyval == GDK_KEY_Return)) {
		buffer_unset_mark(editor->buffer);
		gtk_widget_queue_draw(editor->drar);
		gtk_widget_grab_focus(editor->drar);
		return TRUE;
	}

	if (!shift && ctrl && !alt && !super) {
		if ((event->keyval == GDK_KEY_g) || (event->keyval == GDK_KEY_f)) {
			ctrl_g_invoked = TRUE;
		}
	}

	if (ctrl && (event->keyval == GDK_KEY_r)) {
		history_pick(command_history, editor);
		editor->ignore_next_entry_keyrelease = 1;
		return TRUE;
	}

	move_search_forward(editor, ctrl_g_invoked);

	if (ctrl_g_invoked) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void editor_start_search(editor_t *editor, const char *initial_search_term) {
	buffer_set_mark_at_cursor(editor->buffer);
	editor->label_state = "search";
	gtk_entry_set_text(GTK_ENTRY(editor->entry), (initial_search_term == NULL) ? "" : initial_search_term);
	set_label_text(editor);
	gtk_widget_grab_focus(editor->entry);
	g_signal_handler_disconnect(editor->entry, editor->current_entry_handler_id);
	editor->current_entry_handler_id = g_signal_connect(G_OBJECT(editor->entry), "key-release-event", G_CALLBACK(entry_search_insert_callback), editor);
	editor->search_failed = FALSE;
	editor->search_mode = TRUE;

	/* not needed gtk_entry_set_text seems to generate a move_search_forward through the event automatically
	if (initial_search_term != NULL) move_search_forward(editor, TRUE);
	*/
}

static gboolean entry_default_insert_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	/*
	int shift = event->state & GDK_SHIFT_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;*/

	int ctrl = event->state & GDK_CONTROL_MASK;

	if (editor->ignore_next_entry_keyrelease) {
		editor->ignore_next_entry_keyrelease = 0;
		return TRUE;
	}

	if (cmdcompl_isvisible()) {
		switch (event->keyval) {
		case GDK_KEY_Escape:
			cmdcompl_hide();
			return TRUE;
		case GDK_KEY_Up:
		case GDK_KEY_Down:
			return TRUE;
		case GDK_KEY_Tab:
		case GDK_KEY_Return:
			{
				int point = gtk_editable_get_position(GTK_EDITABLE(editor->entry));
				char *nt = cmdcompl_get_completion(gtk_entry_get_text(GTK_ENTRY(editor->entry)), &point);
				gtk_entry_set_text(GTK_ENTRY(editor->entry), nt);
				gtk_editable_set_position(GTK_EDITABLE(editor->entry), point);
				free(nt);
				cmdcompl_hide();
			}
			return TRUE;
		default:
			cmdcompl_hide();
			return FALSE;
		}
	} else {
		if (event->keyval != GDK_KEY_Tab) {
			cmdcompl_hide();
		}

		if (ctrl && (event->keyval == GDK_KEY_r)) {
			history_pick(command_history, editor);
			editor->ignore_next_entry_keyrelease = 1;
			return TRUE;
		}

		if (event->keyval == GDK_KEY_Escape) {
			gtk_entry_set_text(GTK_ENTRY(editor->entry), "");
			gtk_widget_grab_focus(editor->drar);
			return TRUE;
		}

		if (event->keyval == GDK_KEY_Return) {
			enum deferred_action da;
			if (editor->locked_command_line[0] == '\0') {
				const char *command = gtk_entry_get_text(GTK_ENTRY(editor->entry));
				da = interp_eval(editor, command);
				history_add(command_history, command);
			} else { // locked_command_line was set therefore what was specified is an argument to a pre-established command
				const char *argument = gtk_entry_get_text(GTK_ENTRY(editor->entry));
				if (argument[0] == '\0') {
					da = NOTHING;
				} else {
					const char *eval_args[] = { editor->locked_command_line, argument };
					da = interp_eval(editor, Tcl_Merge(2, eval_args));
				}
			}

			gtk_entry_set_text(GTK_ENTRY(editor->entry), "");
			if (editor->locked_command_line[0] != '\0') {
				strcpy(editor->locked_command_line, "");
				set_label_text(editor);
			}

			switch(da) {
			case FOCUS_ALREADY_SWITCHED:
				break;
			case CLOSE_EDITOR:
				editor_close_editor(editor);
				break;
			default:
				gtk_widget_grab_focus(editor->drar);
			}
			return TRUE;
		}

		if (event->keyval == GDK_KEY_Tab) {
			const char *text;
			int end, i;

			end = gtk_editable_get_position(GTK_EDITABLE(editor->entry));
			text = gtk_entry_get_text(GTK_ENTRY(editor->entry));

			for (i = end-1; i >= 0; --i) {
				if (u_isalnum(text[i])) continue;
				if (text[i] == '-') continue;
				if (text[i] == '_') continue;
				if (text[i] == '/') continue;
				if (text[i] == '~') continue;
				if (text[i] == ':') continue;
				if (text[i] == '.') continue;
				//printf("Breaking on [%c] %d (text: %s)\n", text[i], i, text);
				break;
			}

			//printf("Completion start %d end %d\n", i+1, end);

			if (cmdcompl_complete(text+i+1, end-i-1, editor->buffer->wd) == 1) {
				char *nt = cmdcompl_get_completion(gtk_entry_get_text(GTK_ENTRY(editor->entry)), &end);
				if (nt != NULL) {
					gtk_entry_set_text(GTK_ENTRY(editor->entry), nt);
					gtk_editable_set_position(GTK_EDITABLE(editor->entry), end);
					free(nt);
				}
			} else {
				cmdcompl_show(editor, i+1);
			}

			return TRUE;
		}


	}

	return FALSE;
}

static gboolean entry_key_press_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	switch(event->keyval) {
	case GDK_KEY_Tab:
		history_index_reset(command_history);
		return TRUE;
	case GDK_KEY_Up:
		if (cmdcompl_isvisible()) {
			cmdcompl_move_to_prev();
		} else {
			history_index_next(command_history);
			history_substitute_with_index(command_history, editor);
		}
		return TRUE;
	case GDK_KEY_Down:
		if (cmdcompl_isvisible()) {
			cmdcompl_move_to_next();
		} else {
			history_index_prev(command_history);
			history_substitute_with_index(command_history, editor);
		}
		return TRUE;
	default:
		history_index_reset(command_history);
		return FALSE;
	}
}

static gboolean entry_focusout_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	editor->label_state = "cmd";
	set_label_text(editor);
	if (editor->search_mode) {
		history_add(search_history, gtk_entry_get_text(GTK_ENTRY(editor->entry)));
		gtk_entry_set_text(GTK_ENTRY(editor->entry), "");
	}

	editor->search_mode = FALSE;

	if (editor->locked_command_line[0] != '\0') {
		strcpy(editor->locked_command_line, "");
		set_label_text(editor);
	}

	g_signal_handler_disconnect(editor->entry, editor->current_entry_handler_id);
	editor->current_entry_handler_id = g_signal_connect(editor->entry, "key-release-event", G_CALLBACK(entry_default_insert_callback), editor);
	focus_can_follow_mouse = 1;
	cmdcompl_hide();
	history_index_reset(command_history);
	return FALSE;
}

static gboolean entry_focusin_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	focus_can_follow_mouse = 0;
	return FALSE;
}

void entry_callback_setup(editor_t *r) {
	r->current_entry_handler_id = g_signal_connect(r->entry, "key-release-event", G_CALLBACK(entry_default_insert_callback), r);
	g_signal_connect(r->entry, "key-press-event", G_CALLBACK(entry_key_press_callback), r);

	g_signal_connect(r->entry, "focus-out-event", G_CALLBACK(entry_focusout_callback), r);
	g_signal_connect(r->entry, "focus-in-event", G_CALLBACK(entry_focusin_callback), r);
}

void editor_add_to_command_line(editor_t *editor, const char *text) {
	gtk_widget_grab_focus(editor->entry);

	int i = gtk_editable_get_position(GTK_EDITABLE(editor->entry));
	int j = i;

	gtk_editable_insert_text(GTK_EDITABLE(editor->entry), text, strlen(text), &j);
	gtk_editable_set_position(GTK_EDITABLE(editor->entry), i);
}
