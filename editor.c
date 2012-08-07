#include "editor.h"

#include <math.h>
#include <assert.h>
#include <stdbool.h>

#include <tcl.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"
#include "buffers.h"
#include "interp.h"
#include "columns.h"
#include "column.h"
#include "baux.h"
#include "go.h"
#include "editor_cmdline.h"
#include "cfg.h"
#include "lexy.h"
#include "rd.h"
#include "foundry.h"

static GtkTargetEntry selection_clipboard_target_entry = { "UTF8_STRING", 0, 0 };

static void gtk_teditor_class_init(editor_class *klass);
static void gtk_teditor_init(editor_t *editor);

GType gtk_teditor_get_type(void) {
	static GType teditor_type = 0;

	if (!teditor_type) {
		static const GTypeInfo teditor_info = {
			sizeof(editor_class),
			NULL,
			NULL,
			(GClassInitFunc)gtk_teditor_class_init,
			NULL,
			NULL,
			sizeof(editor_t),
			0,
			(GInstanceInitFunc)gtk_teditor_init,
		};

		teditor_type = g_type_register_static(GTK_TYPE_TABLE, "editor_t", &teditor_info, 0);
	}

	return teditor_type;
}

static void gtk_teditor_class_init(editor_class *class) {
//	GtkWidgetClass *widget_class = (GtkWidgetClass *)class;
}

static void gtk_teditor_init(editor_t *editor) {
}

void set_label_text(editor_t *editor) {
	tframe_t *frame;
	find_editor_for_buffer(editor->buffer, NULL, &frame, NULL);

	tframe_set_title(frame, (editor->buffer->path != NULL) ? editor->buffer->path : editor->buffer->name);
	tframe_set_modified(frame, editor->buffer->modified);
	gtk_widget_queue_draw(GTK_WIDGET(frame));
}

static void editor_absolute_cursor_position(editor_t *editor, double *x, double *y, double *alty) {
	buffer_cursor_position(editor->buffer, x, y);
	*y -= gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	//printf("x = %g y = %g\n", x, y);

	GtkAllocation allocation;
	gtk_widget_get_allocation(editor->drar, &allocation);
	*x += allocation.x; *y += allocation.y;

	gint wpos_x, wpos_y;
	gdk_window_get_position(gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(editor))), &wpos_x, &wpos_y);
	*x += wpos_x; *y += wpos_y;

	*alty = *y - editor->buffer->line_height;
}

static bool editor_maybe_show_completions(editor_t *editor, bool autoinsert) {
	size_t wordcompl_prefix_len;
	uint16_t *prefix = buffer_wordcompl_word_at_cursor(editor->buffer, &wordcompl_prefix_len);

	if (wordcompl_prefix_len == 0) {
		compl_wnd_hide(&word_completer);
		return false;
	}

	char *utf8prefix = string_utf16_to_utf8(prefix, wordcompl_prefix_len);
	char *completion = compl_complete(&word_completer, utf8prefix);

	if (completion != NULL) {
		bool empty_completion = strcmp(completion, "") == 0;

		if (autoinsert && !empty_completion) editor_replace_selection(editor, completion);
		double x, y, alty;
		editor_absolute_cursor_position(editor, &x, &y, &alty);

		if (empty_completion || !autoinsert) {
			compl_wnd_show(&word_completer, utf8prefix, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)), false, false);
		} else {
			char *new_prefix;
			asprintf(&new_prefix, "%s%s", utf8prefix, completion);
			alloc_assert(new_prefix);
			compl_wnd_show(&word_completer, new_prefix, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)), false, false);
			free(new_prefix);
		}
		free(completion);
	} else {
		compl_wnd_hide(&word_completer);
	}

	free(prefix);
	free(utf8prefix);

	return true;
}

void editor_replace_selection(editor_t *editor, const char *new_text) {
	buffer_replace_selection(editor->buffer, new_text);

	column_t *column;
	if (find_editor_for_buffer(editor->buffer, &column, NULL, NULL))
		columns_set_active(columnset, column);

	set_label_text(editor);
	editor_center_on_cursor(editor);
	gtk_widget_queue_draw(editor->drar);

	if (compl_wnd_visible(&word_completer)) {
		editor_maybe_show_completions(editor, false);
	}
}

void editor_center_on_cursor(editor_t *editor) {
	double x, y;
	GtkAllocation allocation;

	gtk_widget_get_allocation(editor->drar, &allocation);
	buffer_cursor_position(editor->buffer, &x, &y);

	double translated_y = y - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
	double translated_x = x - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));

	if ((editor->buffer->cursor.line == NULL) || (editor->buffer->cursor.line->prev == NULL)) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), 0);
	} else {
		if ((translated_y < 0) || (translated_y > allocation.height)) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), y - allocation.height / 2);
		}
	}

	if (editor->buffer->cursor.glyph == 0) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), 0);
	} else {
		if ((translated_x < 0) || (translated_x > allocation.width)) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), x - allocation.width / 2);
		}
	}

}

static void editor_include_cursor(editor_t *editor) {
	double x, y;
	double translated_y;
	GtkAllocation allocation;

	gtk_widget_get_allocation(editor->drar, &allocation);
	buffer_cursor_position(editor->buffer, &x, &y);
	translated_y = y - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	if ((translated_y - editor->buffer->line_height) < 0) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) + (translated_y - editor->buffer->line_height));
	} else if (translated_y > allocation.height) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) + (translated_y - allocation.height));
	}

	if (editor->buffer->rendered_width > allocation.width) {
		double translated_x = x - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));
		if ((translated_x - editor->buffer->em_advance) < 0) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)) + (translated_x - editor->buffer->em_advance));
		} else if (translated_x > allocation.width) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)) + (translated_x - allocation.width));
		}
	}
}

static void editor_get_primary_selection(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, editor_t *editor) {
	lpoint_t start, end;
	char *r = NULL;

	if (editor->buffer->mark_transient) return;

	buffer_get_selection(editor->buffer, &start, &end);

	if (start.line == NULL) return;
	if (end.line == NULL) return;

	r  = buffer_lines_to_text(editor->buffer, &start, &end);

	gtk_selection_data_set_text(selection_data, r, -1);

	free(r);
}

void editor_complete_move(editor_t *editor, gboolean should_move_origin) {
	compl_wnd_hide(&word_completer);
	gtk_widget_queue_draw(editor->drar);

	editor->cursor_visible = TRUE;

	if (should_move_origin) {
		editor_center_on_cursor(editor);
	} else {
		gtk_widget_queue_draw(editor->drar);
	}

	lexy_update_for_move(editor->buffer, editor->buffer->cursor.line);
}

bool editor_move_cursor(editor_t *editor, int delta_line, int delta_char, enum MoveCursorSpecial special, gboolean should_move_origin) {
	bool ret = true; // will stay true if all requested movements can be completed correctly

	while (delta_line < 0) {
		real_line_t *to = editor->buffer->cursor.line->prev;
		if (to == NULL) { ret = false; break; }
		editor->buffer->cursor.line = to;
		++delta_line;
	}

	while (delta_line > 0) {
		real_line_t *to = editor->buffer->cursor.line->next;
		if (to == NULL) { ret = false; break; }
		editor->buffer->cursor.line = to;
		--delta_line;
	}

	if (editor->buffer->cursor.glyph > editor->buffer->cursor.line->cap)
		editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;

	if ((delta_char != 0) || (special != MOVE_NORMAL)) {
		editor->buffer->cursor.glyph += delta_char;

		if (editor->buffer->cursor.glyph < 0) {
			ret = false;
			editor->buffer->cursor.glyph = 0;
		}
		if (editor->buffer->cursor.glyph > editor->buffer->cursor.line->cap) {
			ret = false;
			editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
		}

		if (special == MOVE_LINE_START) {
			editor->buffer->cursor.glyph = 0;
		} else if (special == MOVE_LINE_END) {
			editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
		}
	}

	buffer_extend_selection_by_select_type(editor->buffer);

	editor_complete_move(editor, should_move_origin);

	return ret;
}

static void text_entry_callback(GtkIMContext *context, gchar *str, gpointer data) {
	editor_t *editor = (editor_t *)data;
	//printf("entered: %s\n", str);

	editor_replace_selection(editor, str);
}

void editor_insert_paste(editor_t *editor, GtkClipboard *clipboard) {
	gchar *text = gtk_clipboard_wait_for_text(clipboard);
	if (text == NULL) return;

	editor_replace_selection(editor, text);

	g_free(text);
}

void editor_close_editor(editor_t *editor) {
	tframe_t *frame;
	column_t *column;

	if (!find_editor_for_buffer(editor->buffer, &column, &frame, NULL)) return;

	if (column_frame_number(column) > 1) {
		column_remove(column, frame);
	} else {
		if (editor->buffer == null_buffer()) {
			columns_remove(columnset, column);
		} else {
			editor_switch_buffer(editor, null_buffer());
		}
	}
}

void editor_switch_buffer(editor_t *editor, buffer_t *buffer) {
	editor->buffer = buffer;
	set_label_text(editor);

	{
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		buffer_typeset_maybe(editor->buffer, allocation.width);
	}

	editor->center_on_cursor_after_next_expose = TRUE;
	gtk_widget_queue_draw(editor->drar);
}

static const char *keyevent_to_string(guint keyval) {
	static char ascii[2];

	switch (keyval) {
	case GDK_KEY_BackSpace: return "Backspace";
	case GDK_KEY_Tab: return "Tab";
	case GDK_KEY_Return: return "Return";
	case GDK_KEY_Pause: return "Pause";
	case GDK_KEY_Escape: return "Escape";
	case GDK_KEY_Delete: return "Delete";
	case GDK_KEY_Home: return "Home";
	case GDK_KEY_Left: return "Left";
	case GDK_KEY_Up: return "Up";
	case GDK_KEY_Right: return "Right";
	case GDK_KEY_Down: return "Down";
	case GDK_KEY_Page_Up: return "PageUp";
	case GDK_KEY_Page_Down: return "PageDown";
	case GDK_KEY_End: return "End";
	case GDK_KEY_Insert: return "Insert";
	case GDK_KEY_F1: return "F1";
	case GDK_KEY_F2: return "F2";
	case GDK_KEY_F3: return "F3";
	case GDK_KEY_F4: return "F4";
	case GDK_KEY_F5: return "F5";
	case GDK_KEY_F6: return "F6";
	case GDK_KEY_F7: return "F7";
	case GDK_KEY_F8: return "F8";
	case GDK_KEY_F9: return "F9";
	case GDK_KEY_F10: return "F10";
	case GDK_KEY_F11: return "F11";
	case GDK_KEY_F12: return "F12";
	case GDK_KEY_F13: return "F13";
	case GDK_KEY_F14: return "F14";
	case GDK_KEY_space: return "Space";
	}

	if ((keyval >= 0x21) && (keyval <= 0x7e)) {
		ascii[0] = (char)keyval;
		ascii[1] = 0;
		return ascii;
	} else {
		return NULL;
	}
}

static void set_primary_selection(editor_t *editor) {
	if (!editor->buffer->mark_transient && (editor->buffer->mark.line != NULL)) {
		gtk_clipboard_set_with_data(selection_clipboard, &selection_clipboard_target_entry, 1, (GtkClipboardGetFunc)editor_get_primary_selection, NULL, editor);
	}
}

static void freeze_primary_selection(editor_t *editor) {
	if (editor->buffer->mark_transient) return;
	if (editor->buffer->mark.line == NULL) return;

	lpoint_t start, end;
	char *r = NULL;

	if (editor->buffer->mark_transient) return;

	buffer_get_selection(editor->buffer, &start, &end);

	if (start.line == NULL) return;
	if (end.line == NULL) return;

	r  = buffer_lines_to_text(editor->buffer, &start, &end);

	if (strcmp(r, "") != 0)
		gtk_clipboard_set_text(selection_clipboard, r, -1);

	free(r);
}

void editor_mark_action(editor_t *editor) {
	if (editor->buffer->mark.line == NULL) {
		buffer_set_mark_at_cursor(editor->buffer);
		set_primary_selection(editor);
	} else {
		freeze_primary_selection(editor);
		buffer_unset_mark(editor->buffer);
	}
	gtk_widget_queue_draw(editor->drar);
}

void editor_save_action(editor_t *editor) {
	save_to_text_file(editor->buffer);
	set_label_text(editor);
}

void editor_undo_action(editor_t *editor) {
	buffer_undo(editor->buffer);

	column_t *column;
	if (find_editor_for_buffer(editor->buffer, &column, NULL, NULL))
		columns_set_active(columnset, column);
		
	gtk_widget_queue_draw(editor->drar);
}

static void full_keyevent_to_string(guint keyval, int super, int ctrl, int alt, int shift, char *pressed) {
	const char *converted = keyevent_to_string(keyval);

	if (converted == NULL) {
		pressed[0] = '\0';
		return;
	}

	strcpy(pressed, "");

	if (super) {
		strcat(pressed, "Super-");
	}

	if (ctrl) {
		strcat(pressed, "Ctrl-");
	}

	if (alt) {
		strcat(pressed, "Alt-");
	}

	if (shift) {
		if ((keyval < 0x21) || (keyval > 0x7e)) {
			strcat(pressed, "Shift-");
		}
	}

	strcat(pressed, converted);
}

static void editor_complete(editor_t *editor) {
	char *completion = compl_wnd_get(&word_completer, false);
	compl_wnd_hide(&word_completer);
	if (completion != NULL) {
		editor_replace_selection(editor, completion);
		free(completion);
	}
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	char pressed[40] = "";
	const char *command;
	GtkAllocation allocation;
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	gtk_widget_get_allocation(editor->drar, &allocation);

	if (editor->buffer->keyprocessor != NULL) {
		printf("Keyprocessor invocation\n");
		full_keyevent_to_string(event->keyval, super, ctrl, alt, shift, pressed);
		if (pressed[0] != '\0') {
			compl_wnd_hide(&word_completer);
			const char *eval_argv[] = { editor->buffer->keyprocessor, pressed };
			const char *r = interp_eval_command(2, eval_argv);

			if ((r != NULL) && (strcmp(r, "done") == 0)) return FALSE;
		}
	}

	if (!ctrl && !alt && !super) {
		switch(event->keyval) {
		case GDK_KEY_Delete:
			if (editor->buffer->mark.line == NULL) {
				buffer_set_mark_at_cursor(editor->buffer);
				buffer_move_cursor(editor->buffer, +1);
				editor_replace_selection(editor, "");
			} else {
				editor_replace_selection(editor, "");
			}
			return TRUE;
		case GDK_KEY_BackSpace:
			if (editor->buffer->mark.line == NULL) {
				buffer_set_mark_at_cursor(editor->buffer);
				buffer_move_cursor(editor->buffer, -1);
				editor_replace_selection(editor, "");
			} else {
				editor_replace_selection(editor, "");
			}
			return TRUE;
		}
	}

	/* Default key bindings */
	if (!shift && !ctrl && !alt && !super) {
		if (compl_wnd_visible(&word_completer)) {
			switch(event->keyval) {
				case GDK_KEY_Up:
					compl_wnd_up(&word_completer);
					return TRUE;
				case GDK_KEY_Down:
				case GDK_KEY_Tab:
					if (word_completer.common_suffix != NULL) {
						editor_replace_selection(editor, word_completer.common_suffix);
					} else {
						compl_wnd_down(&word_completer);
					}
					return TRUE;
				case GDK_KEY_Escape:
				case GDK_KEY_Left:
					return FALSE;
				case GDK_KEY_Return:
				case GDK_KEY_Right: {
					editor_complete(editor);
					return TRUE;
				}
			}
		}

		switch(event->keyval) {
		case GDK_KEY_Up:
			editor_move_cursor(editor, -1, 0, MOVE_NORMAL, TRUE);
			return TRUE;
		case GDK_KEY_Down:
			editor_move_cursor(editor, 1, 0, MOVE_NORMAL, TRUE);
			return TRUE;
		case GDK_KEY_Right:
			editor_move_cursor(editor, 0, 1, MOVE_NORMAL, TRUE);
			return TRUE;
		case GDK_KEY_Left:
			editor_move_cursor(editor, 0, -1, MOVE_NORMAL, TRUE);
			return TRUE;

		case GDK_KEY_Page_Up:
			editor_move_cursor(editor, -(allocation.height / editor->buffer->line_height) + 2, 0, MOVE_NORMAL, TRUE);
			//gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) - gtk_adjustment_get_page_increment(GTK_ADJUSTMENT(editor->adjustment)));
			return TRUE;
		case GDK_KEY_Page_Down:
			editor_move_cursor(editor, +(allocation.height / editor->buffer->line_height) - 2, 0, MOVE_NORMAL, TRUE);
			return TRUE;

		case GDK_KEY_Home:
			buffer_aux_go_first_nonws_or_0(editor->buffer);
			editor_complete_move(editor, TRUE);
			return TRUE;
		case GDK_KEY_End:
			editor_move_cursor(editor, 0, 0, MOVE_LINE_END, TRUE);
			return TRUE;

		case GDK_KEY_Tab: {
			if (!editor_maybe_show_completions(editor, true)) {
				editor_replace_selection(editor, "\t");
			}

			return TRUE;
		}

		case GDK_KEY_Return: {
			if (buffer_aux_is_directory(editor->buffer)) {
				rd_open(editor);
				gtk_widget_queue_draw(editor->drar);
			} else {
				char *r = alloca(sizeof(char) * (editor->buffer->cursor.line->cap + 2));
				if (config[CFG_DEFAULT_AUTOINDENT].intval) {
					buffer_indent_newline(editor->buffer, r);
				} else {
					r[0] = '\n';
					r[1] = '\0';
				}
				editor_replace_selection(editor, r);
				return TRUE;
			}
		}
		case GDK_KEY_Escape:
			return TRUE;
		default: // At least one modifier must be pressed to activate special actions
			goto im_context;
		}
	}

	if (!shift && ctrl && !alt && !super && (event->keyval == GDK_KEY_Tab)) {
		editor_replace_selection(editor, "\t");
		return TRUE;
	}

	if (shift && !ctrl && !alt && !super) {
		if ((event->keyval >= 0x20) && (event->keyval <= 0x7e)) {
			goto im_context;
		}
	}

	compl_wnd_hide(&word_completer);

	if (strcmp(pressed, "") == 0) {
		full_keyevent_to_string(event->keyval, super, ctrl, alt, shift, pressed);
	}

	if (pressed[0] == '\0') goto im_context;

	command = g_hash_table_lookup(keybindings, pressed);
	//printf("Keybinding [%s] -> {%s}\n", pressed, command);

	if (command != NULL) {
		interp_eval(editor, command, false);
	}

	return TRUE;

 im_context:
	/* Normal text input processing */
	if (gtk_im_context_filter_keypress(editor->drarim, event)) {
		return TRUE;
	}

	/*printf("Unknown key sequence: %d (shift %d ctrl %d alt %d super %d)\n", event->keyval, shift, ctrl, alt, super);*/

	return TRUE;
}

static gboolean key_release_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	if (!shift && !ctrl && !alt && !super) {
		switch(event->keyval) {
		case GDK_KEY_Escape:
			if (compl_wnd_visible(&word_completer)) {
				compl_wnd_hide(&word_completer);
			} else {
				// TODO: move focus to command line
			}
			return TRUE;
		}
	}

	return FALSE;
}

static void move_cursor_to_mouse(editor_t *editor, double x, double y) {
	GtkAllocation allocation;

	gtk_widget_get_allocation(editor->drar, &allocation);

	x += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));
	y += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	buffer_move_cursor_to_position(editor->buffer, x, y);
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, editor_t *editor) {
	if (selection_target_buffer != NULL) {
		editor_switch_buffer(editor, selection_target_buffer);
		selection_target_buffer = NULL;
		return TRUE;
	}

	gtk_widget_grab_focus(editor->drar);

	if (event->button == 1) {
		move_cursor_to_mouse(editor, event->x, event->y);

		editor->mouse_marking = 1;
		buffer_set_mark_at_cursor(editor->buffer);

		if (event->type == GDK_2BUTTON_PRESS) {
			buffer_change_select_type(editor->buffer, BST_WORDS);
			set_primary_selection(editor);
		} else if (event->type == GDK_3BUTTON_PRESS) {
			buffer_change_select_type(editor->buffer, BST_LINES);
			set_primary_selection(editor);
		}

		editor_complete_move(editor, TRUE);
	} else if (event->button == 2) {
		move_cursor_to_mouse(editor, event->x, event->y);
		buffer_unset_mark(editor->buffer);
		editor_complete_move(editor, TRUE);
		editor_insert_paste(editor, selection_clipboard);
	} else if (event->button == 3) {
		lpoint_t start, end;

		buffer_get_selection(editor->buffer, &start, &end);

		buffer_unset_mark(editor->buffer);
		move_cursor_to_mouse(editor, event->x, event->y);
		editor_complete_move(editor, TRUE);

		// here we check if the new cursor position (the one we created by clicking with the mouse) is inside the old selection area, in that case we do execute the mouse_open_action function on the selection. If no selection was active then we create one around the cursor and execute the mouse_open_action function on that
		if (start.line == NULL) {
			copy_lpoint(&start, &(editor->buffer->cursor));
			mouse_open_action(editor, &start, NULL, -1);
		} else if (inbetween_lpoint(&start, &(editor->buffer->cursor), &end)) {
			mouse_open_action(editor, &start, &end, -1);
		}
	}

	return TRUE;
}

static gboolean button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	editor_t *editor = (editor_t*)data;

	editor->mouse_marking = 0;

	if ((editor->buffer->mark.line == editor->buffer->cursor.line) && (editor->buffer->mark.glyph == editor->buffer->cursor.glyph)) {
		editor->buffer->mark.line = NULL;
		editor->buffer->mark.glyph = -1;
		gtk_widget_queue_draw(editor->drar);
	} else {
		freeze_primary_selection(editor);
	}

	return TRUE;
}

static gboolean scroll_callback(GtkWidget *widget, GdkEventScroll *event, editor_t *editor) {
	switch(event->direction) {
	case GDK_SCROLL_UP: {
		double nv = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) - editor->buffer->line_height;
		if (nv < 0) nv = 0;
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), nv);
		break;
	}
	case GDK_SCROLL_DOWN: {
		double nv = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) + editor->buffer->line_height;
		double mv = gtk_adjustment_get_upper(GTK_ADJUSTMENT(editor->adjustment)) - gtk_adjustment_get_page_size(GTK_ADJUSTMENT(editor->adjustment));
		if (nv > mv) nv = mv;
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), nv);
		break;
	}
	default:
		// no scroll left/right for now
		break;
	}

	return TRUE;
}

static void selection_move(editor_t *editor, double x, double y) {
	move_cursor_to_mouse(editor, x, y);
	gtk_clipboard_set_with_data(selection_clipboard, &selection_clipboard_target_entry, 1, (GtkClipboardGetFunc)editor_get_primary_selection, NULL, editor);
	//editor_center_on_cursor(editor);
	editor_include_cursor(editor);
	gtk_widget_queue_draw(editor->drar);
}

static gboolean selection_autoscroll(editor_t *editor) {
	gint x, y;
	gtk_widget_get_pointer(editor->drar, &x, &y);
	selection_move(editor, x, y);
	return TRUE;
}

static void start_selection_scroll(editor_t *editor) {
	if (editor->selection_scroll_timer >= 0) return;
	editor->selection_scroll_timer = g_timeout_add(AUTOSCROLL_TIMO, (GSourceFunc)selection_autoscroll, (gpointer)editor);
}

static void end_selection_scroll(editor_t *editor) {
	if (editor->selection_scroll_timer < 0) return;
	g_source_remove(editor->selection_scroll_timer);
	editor->selection_scroll_timer = -1;
}

static gboolean motion_callback(GtkWidget *widget, GdkEventMotion *event, editor_t *editor) {
	if (editor->mouse_marking) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);

		gdouble x = event->x + allocation.x, y = event->y + allocation.y;

		//printf("inside allocation x = %d y = %d (x = %d y = %d height = %d width = %d)\n", (int)x, (int)y, (int)allocation.x, (int)allocation.y, (int)allocation.height, (int)allocation.width);

		if (inside_allocation(x, y, &allocation)) {
			end_selection_scroll(editor);
			selection_move(editor, event->x, event->y);
		} else {
			start_selection_scroll(editor);
		}

		set_primary_selection(editor);
	} else {
		end_selection_scroll(editor);
	}

	// focus follows mouse
	if ((config[CFG_FOCUS_FOLLOWS_MOUSE].intval) && focus_can_follow_mouse) {
		if (!gtk_widget_is_focus(editor->drar)) {
			gtk_widget_grab_focus(editor->drar);
			gtk_widget_queue_draw(editor->drar);
		}
	}

	return TRUE;
}

static void draw_selection(editor_t *editor, double width, cairo_t *cr) {
	lpoint_t start, end;
	double selstart_y, selend_y;
	double selstart_x, selend_x;

	if (editor->buffer->mark.glyph == -1) return;

	buffer_get_selection(editor->buffer, &start, &end);

	if ((start.line == end.line) && (start.glyph == end.glyph)) return;

	line_get_glyph_coordinates(editor->buffer, &start, &selstart_x, &selstart_y);
	line_get_glyph_coordinates(editor->buffer, &end, &selend_x, &selend_y);

	set_color_cfg(cr, config[CFG_EDITOR_SEL_COLOR].intval);

	if (fabs(selstart_y - selend_y) < 0.001) {
		cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
		cairo_rectangle(cr, selstart_x, selstart_y-editor->buffer->ascent, selend_x - selstart_x, editor->buffer->ascent + editor->buffer->descent);
		cairo_fill(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	} else {
		cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);

		// start of selection
		cairo_rectangle(cr, selstart_x, selstart_y-editor->buffer->ascent, width - selstart_x, editor->buffer->ascent + editor->buffer->descent);
		cairo_fill(cr);

		// middle of selection
		cairo_rectangle(cr, 0.0, selstart_y + editor->buffer->descent, width, selend_y - editor->buffer->ascent - editor->buffer->descent - selstart_y);
		cairo_fill(cr);

		// end of selection
		cairo_rectangle(cr, 0.0, selend_y-editor->buffer->ascent, selend_x, editor->buffer->ascent + editor->buffer->descent);
		cairo_fill(cr);

		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	}

	set_color_cfg(cr, config[CFG_EDITOR_FG_COLOR].intval);
}

static void draw_parmatch(editor_t *editor, GtkAllocation *allocation, cairo_t *cr) {
	buffer_update_parmatch(editor->buffer);

	if (editor->buffer->parmatch.matched.line == NULL) return;

	double x, y;
	line_get_glyph_coordinates(editor->buffer, &(editor->buffer->parmatch.matched), &x, &y);

	cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);

	cairo_rectangle(cr, x, y - editor->buffer->ascent, LPOINTGI(editor->buffer->parmatch.matched).x_advance, editor->buffer->ascent + editor->buffer->descent);
	cairo_fill(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

struct growable_glyph_array {
	cairo_glyph_t *glyphs;
	int n;
	int allocated;
	uint16_t kind;
};

static struct growable_glyph_array *growable_glyph_array_init(void) {
	struct growable_glyph_array *gga = malloc(sizeof(struct growable_glyph_array));
	gga->allocated = 10;
	gga->n = 0;
	gga->kind = 0;
	gga->glyphs = malloc(sizeof(cairo_glyph_t) * gga->allocated);
	alloc_assert(gga->glyphs);
	return gga;
}

static void growable_glyph_array_free(struct growable_glyph_array *gga) {
	free(gga->glyphs);
	free(gga);
}

static void growable_glyph_array_append(struct growable_glyph_array *gga, cairo_glyph_t glyph) {
	if (gga->n >= gga->allocated) {
		gga->allocated *= 2;
		gga->glyphs = realloc(gga->glyphs, sizeof(cairo_glyph_t) * gga->allocated);
	}

	gga->glyphs[gga->n] = glyph;
	++(gga->n);
}

static void draw_line(editor_t *editor, GtkAllocation *allocation, cairo_t *cr, real_line_t *line, GHashTable *ht) {
	struct growable_glyph_array *gga_current = NULL;

	double cury = line->start_y;

	for (int i = 0; i < line->cap; ++i) {
		// draws soft wrvoid compl_wnd_hide(struct completer capping indicators
		if (line->glyph_info[i].y - cury > 0.001) {
			/* draw ending tract */
			cairo_set_line_width(cr, 4.0);
			cairo_move_to(cr, line->glyph_info[i-1].x + line->glyph_info[i-1].x_advance, cury-(editor->buffer->ex_height/2.0));
			cairo_line_to(cr, allocation->width, cury-(editor->buffer->ex_height/2.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 2.0);

			cury = line->glyph_info[i].y;

			/* draw initial tract */
			cairo_set_line_width(cr, 4.0);
			cairo_move_to(cr, 0.0, cury-(editor->buffer->ex_height/2.0));
			cairo_line_to(cr, editor->buffer->left_margin, cury-(editor->buffer->ex_height/2.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 2.0);
		}

		uint16_t type = (uint16_t)(line->glyph_info[i].color) + ((uint16_t)(line->glyph_info[i].fontidx) << 8);

		if ((gga_current == NULL) || (gga_current->kind != type)) {
			gga_current = g_hash_table_lookup(ht, (gconstpointer)(uint64_t)type);
			if (gga_current == NULL) {
				gga_current = growable_glyph_array_init();
				gga_current->kind = type;
				g_hash_table_insert(ht, (gpointer)(uint64_t)type, gga_current);
			}
		}

		cairo_glyph_t g;
		g.index = line->glyph_info[i].glyph_index;
		g.x = line->glyph_info[i].x;
		g.y = line->glyph_info[i].y;

		growable_glyph_array_append(gga_current, g);
	}
}

static void draw_cursorline(cairo_t *cr, editor_t *editor) {
	if (!(editor->cursor_visible)) return;
	if (editor->buffer->cursor.line == NULL) return;
	GtkAllocation allocation;

	gtk_widget_get_allocation(editor->drar, &allocation);

	double cursor_x, cursor_y;
	buffer_cursor_position(editor->buffer, &cursor_x, &cursor_y);

	set_color_cfg(cr, config[CFG_EDITOR_BG_CURSORLINE].intval);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle(cr, cursor_x - allocation.width, cursor_y-editor->buffer->ascent, 2*allocation.width, editor->buffer->ascent+editor->buffer->descent);
	cairo_fill(cr);
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, editor_t *editor) {
	cairo_t *cr = gdk_cairo_create(widget->window);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	if (selection_target_buffer != NULL) {
		gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_ICON));
	} else if (buffer_aux_is_directory(editor->buffer)) {
		gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_ARROW));
	} else {
		gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_XTERM));
	}

	set_color_cfg(cr, config[CFG_EDITOR_BG_COLOR].intval);
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	cairo_translate(cr, -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

	draw_cursorline(cr, editor);

	buffer_typeset_maybe(editor->buffer, allocation.width);

	editor->buffer->rendered_height = 0.0;

	double originy = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	GHashTable *ht = g_hash_table_new(g_direct_hash, g_direct_equal);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	set_color_cfg(cr, config[CFG_EDITOR_FG_COLOR].intval);

	int count = 0;
	for (real_line_t *line = editor->buffer->real_line; line != NULL; line = line->next) {
		if (((line->start_y + line->y_increment - originy) > 0) && ((line->start_y - editor->buffer->ascent - originy) < allocation.height)) {
			draw_line(editor, &allocation, cr, line, ht);
		}

		++count;

		editor->buffer->rendered_height += line->y_increment;
	}

	{
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		GHashTableIter it;
		g_hash_table_iter_init(&it, ht);
		uint16_t type;
		struct growable_glyph_array *gga;
		while (g_hash_table_iter_next(&it, (gpointer *)&type, (gpointer *)&gga)) {
			uint8_t color = (uint8_t)type;
			uint8_t fontidx = (uint8_t)(type >> 8);
			//printf("Printing text with font %d, color %d\n", fontidx, color);
			cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config[CFG_MAIN_FONT].strval, fontidx));
			set_color_cfg(cr, lexy_colors[color]);
			cairo_show_glyphs(cr, gga->glyphs, gga->n);
			growable_glyph_array_free(gga);
		}
	}

	//printf("\n\n");

	g_hash_table_destroy(ht);

	draw_selection(editor, allocation.width, cr);
	draw_parmatch(editor, &allocation, cr);

	if (editor->cursor_visible && !(editor->search_mode)) {
		double cursor_x, cursor_y;

		buffer_cursor_position(editor->buffer, &cursor_x, &cursor_y);

		cairo_rectangle(cr, cursor_x, cursor_y-editor->buffer->ascent, 2, editor->buffer->ascent+editor->buffer->descent);
		cairo_fill(cr);
	}

	/********** NOTHING IS TRANSLATED BEYOND THIS ***************************/
	cairo_translate(cr, gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));


	{
		char *posbox_text;
		cairo_text_extents_t posbox_ext;
		double x, y;

		asprintf(&posbox_text, " %d,%d %0.0f%%", editor->buffer->cursor.line->lineno+1, editor->buffer->cursor.glyph, (100.0 * editor->buffer->cursor.line->lineno / count));

		cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config[CFG_POSBOX_FONT].strval, 0));

		cairo_text_extents(cr, posbox_text, &posbox_ext);

		y = allocation.height - posbox_ext.height - 4.0;
		x = allocation.width - posbox_ext.x_advance - 4.0;

		set_color_cfg(cr, config[CFG_POSBOX_BORDER_COLOR].intval);
		cairo_rectangle(cr, x-1.0, y-1.0, posbox_ext.x_advance+4.0, posbox_ext.height+4.0);
		cairo_fill(cr);
		set_color_cfg(cr, config[CFG_POSBOX_BG_COLOR].intval);
		cairo_rectangle(cr, x, y, posbox_ext.x_advance + 2.0, posbox_ext.height + 2.0);
		cairo_fill(cr);

		cairo_move_to(cr, x+1.0, y+posbox_ext.height);
		set_color_cfg(cr, config[CFG_POSBOX_FG_COLOR].intval);
		cairo_show_text(cr, posbox_text);

		free(posbox_text);
	}

	gtk_adjustment_set_upper(GTK_ADJUSTMENT(editor->adjustment), editor->buffer->rendered_height + (allocation.height / 2));
	gtk_adjustment_set_page_size(GTK_ADJUSTMENT(editor->adjustment), allocation.height);
	gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(editor->adjustment), allocation.height/2);
	gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(editor->adjustment), editor->buffer->line_height);

	if (editor->buffer->rendered_width <= allocation.width) {
		gtk_widget_hide(GTK_WIDGET(editor->drarhscroll));
	} else {
		gtk_widget_show(GTK_WIDGET(editor->drarhscroll));
		gtk_adjustment_set_upper(GTK_ADJUSTMENT(editor->hadjustment), editor->buffer->left_margin + editor->buffer->rendered_width + editor->buffer->right_margin);
		gtk_adjustment_set_page_size(GTK_ADJUSTMENT(editor->hadjustment), allocation.width);
		gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(editor->hadjustment), allocation.width/2);
		gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(editor->hadjustment), editor->buffer->em_advance);
	}

	cairo_destroy(cr);

	if (editor->center_on_cursor_after_next_expose) {
		editor->center_on_cursor_after_next_expose = FALSE;
		editor_center_on_cursor(editor);
	}

	if (editor->warp_mouse_after_next_expose) {
		editor_grab_focus(editor, true);
		editor->warp_mouse_after_next_expose = FALSE;
	}

	return TRUE;
}

static gboolean scrolled_callback(GtkAdjustment *adj, gpointer data) {
	editor_t *editor = (editor_t *)data;
	//printf("Scrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)));
	gtk_widget_queue_draw(editor->drar);
	return TRUE;
}

static gboolean hscrolled_callback(GtkAdjustment *adj, gpointer data) {
	editor_t *editor = (editor_t *)data;
	//printf("HScrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment)));
	gtk_widget_queue_draw(editor->drar);
	return TRUE;
}

static gboolean editor_focusin_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	editor->cursor_visible = 1;
	gtk_widget_queue_draw(editor->drar);
	return FALSE;
}

static gboolean editor_focusout_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	compl_wnd_hide(&word_completer);
	editor->cursor_visible = 0;
	gtk_widget_queue_draw(editor->drar);
	end_selection_scroll(editor);
	return FALSE;
}

editor_t *new_editor(buffer_t *buffer) {
	GtkWidget *editor_widget = g_object_new(GTK_TYPE_TEDITOR, NULL);
	editor_t *r = GTK_TEDITOR(editor_widget);

	r->buffer = buffer;
	r->cursor_visible = TRUE;
	r->mouse_marking = 0;
	r->ignore_next_entry_keyrelease = FALSE;
	r->center_on_cursor_after_next_expose = FALSE;
	r->warp_mouse_after_next_expose = FALSE;

	r->search_mode = FALSE;
	r->search_failed = FALSE;

	r->drar = gtk_drawing_area_new();
	r->drarim = gtk_im_multicontext_new();

	r->selection_scroll_timer = -1;

	gtk_widget_add_events(r->drar, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

	gtk_widget_set_can_focus(GTK_WIDGET(r->drar), TRUE);

	g_signal_connect(G_OBJECT(r->drar), "expose_event", G_CALLBACK(expose_event_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "key-press-event", G_CALLBACK(key_press_callback), r);
	g_signal_connect(G_OBJECT(r->drar), "key-release-event", G_CALLBACK(key_release_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "button-press-event", G_CALLBACK(button_press_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "button-release-event", G_CALLBACK(button_release_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "scroll-event", G_CALLBACK(scroll_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "motion-notify-event", G_CALLBACK(motion_callback), r);
    g_signal_connect(G_OBJECT(r->drar), "focus-out-event", G_CALLBACK(editor_focusout_callback), r);
    g_signal_connect(G_OBJECT(r->drar), "focus-in-event", G_CALLBACK(editor_focusin_callback), r);

	g_signal_connect(G_OBJECT(r->drarim), "commit", G_CALLBACK(text_entry_callback), r);


	GtkWidget *drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(r->adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
	r->drarhscroll = gtk_hscrollbar_new((GtkAdjustment *)(r->hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));

	gtk_table_attach(GTK_TABLE(r), r->drar, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(r), drarscroll, 1, 2, 0, 1, 0, GTK_EXPAND|GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(r), r->drarhscroll, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	g_signal_connect(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(r->drarhscroll), "value_changed", G_CALLBACK(hscrolled_callback), (gpointer)r);

	return r;
}

void editor_grab_focus(editor_t *editor, bool warp) {
	gtk_widget_grab_focus(editor->drar);

	if (config[CFG_WARP_MOUSE].intval && warp) {
		GdkDisplay *display = gdk_display_get_default();
		GdkScreen *screen = gdk_display_get_default_screen(display);
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		if ((allocation.x < 0) || (allocation.y < 0)) {
			editor->warp_mouse_after_next_expose = TRUE;
		} else {
			gint wpos_x, wpos_y;
			gdk_window_get_position(gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(editor))), &wpos_x, &wpos_y);

			//printf("allocation: %d,%d\n", allocation.x, allocation.y);
			gdk_display_warp_pointer(display, screen, allocation.x+wpos_x+5, allocation.y+wpos_y+5);
		}
	}
}

