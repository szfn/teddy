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
#include "cfg.h"
#include "lexy.h"
#include "foundry.h"
#include "top.h"

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
	//GtkWidgetClass *widget_class = (GtkWidgetClass *)class;
	//widget_class->size_request = gtk_teditor_size_request;
}

static void gtk_teditor_init(editor_t *editor) {
}

static void set_label_text(editor_t *editor) {
	tframe_t *frame;
	find_editor_for_buffer(editor->buffer, NULL, &frame, NULL);

	if (frame != NULL) {
		tframe_set_title(frame, editor->buffer->path);
		tframe_set_modified(frame, editor->buffer->modified);
		gtk_widget_queue_draw(GTK_WIDGET(frame));
	}
}

static void dirty_line_update(editor_t *editor) {
	editor->dirty_line = false;
	buffer_wordcompl_update_line(editor->buffer->cursor.line, &(editor->buffer->cbt));
	word_completer_full_update();
}

void editor_absolute_cursor_position(editor_t *editor, double *x, double *y, double *alty) {
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
	uint16_t *prefix = editor->completer->prefix_from_buffer(editor->completer->this, editor->buffer, &wordcompl_prefix_len);

	if (wordcompl_prefix_len <= (autoinsert ? 0 : 2)) {
		COMPL_WND_HIDE(editor->completer);
		return false;
	}

	char *utf8prefix = string_utf16_to_utf8(prefix, wordcompl_prefix_len);
	char *completion = COMPL_COMPLETE(editor->completer, utf8prefix);

	if (completion != NULL) {
		bool empty_completion = strcmp(completion, "") == 0;

		if (autoinsert && !empty_completion) editor_replace_selection(editor, completion);
		double x, y, alty;
		editor_absolute_cursor_position(editor, &x, &y, &alty);

		if (empty_completion || !autoinsert) {
			COMPL_WND_SHOW(editor->completer, utf8prefix, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)));
		} else {
			char *new_prefix;
			asprintf(&new_prefix, "%s%s", utf8prefix, completion);
			alloc_assert(new_prefix);
			COMPL_WND_SHOW(editor->completer, new_prefix, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)));
			free(new_prefix);
		}
		free(completion);
	} else {
		COMPL_WND_HIDE(editor->completer);
	}

	free(prefix);
	free(utf8prefix);

	return true;
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

void editor_replace_selection(editor_t *editor, const char *new_text) {
	buffer_replace_selection(editor->buffer, new_text);

	column_t *column;
	if (find_editor_for_buffer(editor->buffer, &column, NULL, NULL))
		columns_set_active(columnset, column);

	set_label_text(editor);
	editor_center_on_cursor(editor);
	gtk_widget_queue_draw(editor->drar);

	editor->dirty_line = true;

	editor_maybe_show_completions(editor, false);
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

	buffer_get_selection(editor->buffer, &start, &end);

	if (start.line == NULL) return;
	if (end.line == NULL) return;

	r  = buffer_lines_to_text(editor->buffer, &start, &end);

	gtk_selection_data_set_text(selection_data, r, -1);

	free(r);
}

void editor_complete_move(editor_t *editor, gboolean should_move_origin) {
	COMPL_WND_HIDE(editor->completer);
	gtk_widget_queue_draw(editor->drar);
	editor->cursor_visible = TRUE;
	if (should_move_origin) editor_center_on_cursor(editor);
	lexy_update_for_move(editor->buffer, editor->buffer->cursor.line);
}

static void text_entry_callback(GtkIMContext *context, gchar *str, editor_t *editor) {
	editor_replace_selection(editor, str);
}

void editor_switch_buffer(editor_t *editor, buffer_t *buffer) {
	editor->buffer = buffer;
	set_label_text(editor);

	{
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		buffer_typeset_maybe(editor->buffer, allocation.width, editor->single_line, false);
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
	if (editor->buffer->mark.line != NULL) {
		gtk_clipboard_set_with_data(selection_clipboard, &selection_clipboard_target_entry, 1, (GtkClipboardGetFunc)editor_get_primary_selection, NULL, editor);
	}
}

static void freeze_primary_selection(editor_t *editor) {
	if (editor->buffer->mark.line == NULL) return;

	lpoint_t start, end;
	char *r = NULL;

	buffer_get_selection(editor->buffer, &start, &end);

	if (start.line == NULL) return;
	if (end.line == NULL) return;

	r  = buffer_lines_to_text(editor->buffer, &start, &end);

	if (strcmp(r, "") != 0)
		gtk_clipboard_set_text(selection_clipboard, r, -1);

	free(r);
}

static bool mark_move(editor_t *editor, bool shift) {
	if (editor->buffer->mark.line == NULL) {
		if (shift) {
			buffer_set_mark_at_cursor(editor->buffer);
			set_primary_selection(editor);
		}
		return true;
	} else {
		if (!shift) {
			//freeze_primary_selection(editor);
			buffer_unset_mark(editor->buffer);
			return false;
		} else {
			return true;
		}
	}
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

	if (super) strcat(pressed, "Super-");
	if (ctrl) strcat(pressed, "Ctrl-");
	if (alt) strcat(pressed, "Alt-");

	if (shift) {
		if ((keyval < 0x21) || (keyval > 0x7e)) {
			strcat(pressed, "Shift-");
		}
	}

	strcat(pressed, converted);
}

static void editor_complete(editor_t *editor) {
	char *completion = COMPL_WND_GET(editor->completer, false);
	COMPL_WND_HIDE(editor->completer);
	if (completion != NULL) {
		editor_replace_selection(editor, completion);
		free(completion);
	}
}

static void menu_position_function(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, editor_t *editor) {
	*push_in = TRUE;

	double dx, dy, alty;
	editor_absolute_cursor_position(editor, &dx, &dy, &alty);
	*x = (int)dx;
	*y = (int)dy;
}

static void select_file(buffer_t *buffer, lpoint_t *p, lpoint_t *start, lpoint_t *end) {
	start->line = p->line; end->line = p->line;
	for (start->glyph = p->glyph; start->glyph > 0; --(start->glyph))
		if (p->line->glyph_info[start->glyph].color != CFG_LEXY_FILE - CFG_LEXY_NOTHING) {
			++(start->glyph);
			break;
		}

	for (end->glyph = p->glyph; end->glyph < p->line->cap; ++(end->glyph))
		if (p->line->glyph_info[end->glyph].color != CFG_LEXY_FILE - CFG_LEXY_NOTHING) break;
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
			COMPL_WND_HIDE(editor->completer);
			const char *eval_argv[] = { editor->buffer->keyprocessor, pressed };
			const char *r = interp_eval_command(editor, NULL, 2, eval_argv);

			if ((r != NULL) && (strcmp(r, "done") == 0)) return FALSE;
		}
	}

	/* Manipulation of completions window */
	if (!shift && !ctrl && !alt && !super) {
		if (COMPL_WND_VISIBLE(editor->completer)) {
			switch(event->keyval) {
				case GDK_KEY_Up:
					COMPL_WND_UP(editor->completer);
					return TRUE;
				case GDK_KEY_Down:
				case GDK_KEY_Tab:
					if (COMPL_COMMON_SUFFIX(editor->completer) != NULL) {
						editor_replace_selection(editor, COMPL_COMMON_SUFFIX(editor->completer));
					} else {
						COMPL_WND_DOWN(editor->completer);
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
		} else if (editor->single_line) {
			if (editor->single_line_other_keys(editor, shift, ctrl, alt, super, event->keyval)) {
				return TRUE;
			}
		}
	}

	/* Motion and keys that are invariant to shift */
	if (!ctrl && !alt && !super) {
		switch(event->keyval) {
		case GDK_KEY_Delete:
			if (editor->buffer->mark.line == NULL) {
				buffer_set_mark_at_cursor(editor->buffer);
				buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_REL, +1);
			}
			editor_replace_selection(editor, "");
			return TRUE;

		case GDK_KEY_BackSpace:
			if (editor->buffer->mark.line == NULL) {
				buffer_set_mark_at_cursor(editor->buffer);
				buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_REL, -1);
			}
			editor_replace_selection(editor, "");
			return TRUE;

		case GDK_KEY_Up:
			dirty_line_update(editor);
			if (mark_move(editor, shift)) buffer_move_point_line(editor->buffer, &(editor->buffer->cursor), MT_REL, -1);
			goto key_press_return_true;
		case GDK_KEY_Down:
			dirty_line_update(editor);
			if (mark_move(editor, shift)) buffer_move_point_line(editor->buffer, &(editor->buffer->cursor), MT_REL, +1);
			goto key_press_return_true;
		case GDK_KEY_Right:
			if (mark_move(editor, shift)) buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_REL, +1);
			goto key_press_return_true;
		case GDK_KEY_Left:
			if (mark_move(editor, shift)) buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_REL, -1);
			goto key_press_return_true;

		case GDK_KEY_Page_Up:
			dirty_line_update(editor);
			mark_move(editor, shift);
			buffer_move_point_line(editor->buffer, &(editor->buffer->cursor), MT_REL, -(allocation.height / editor->buffer->line_height) + 2);
			goto key_press_return_true;
		case GDK_KEY_Page_Down:
			dirty_line_update(editor);
			mark_move(editor, shift);
			buffer_move_point_line(editor->buffer, &(editor->buffer->cursor), MT_REL, +(allocation.height / editor->buffer->line_height) - 2);
			goto key_press_return_true;

		case GDK_KEY_Home:
			mark_move(editor, shift);
			buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_HOME, 0);
			goto key_press_return_true;
		case GDK_KEY_End:
			mark_move(editor, shift);
			buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_END, 0);
			goto key_press_return_true;
		}
	}

	/* Other default keybindings */
	if (!shift && !ctrl && !alt && !super) {
		switch(event->keyval) {
		case GDK_KEY_Tab: {
			if (!editor_maybe_show_completions(editor, true)) {
				editor_replace_selection(editor, "\t");
			}

			return TRUE;
		}

		case GDK_KEY_Menu: {
			lpoint_t start, end;
			buffer_get_selection(editor->buffer, &start, &end);

			if (((start.line != NULL) && (end.line != NULL)) || ((editor->buffer->cursor.glyph < editor->buffer->cursor.line->cap) && (editor->buffer->cursor.line->glyph_info[editor->buffer->cursor.glyph].color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING))))
				gtk_menu_popup(GTK_MENU(editor->context_menu), NULL, NULL, (GtkMenuPositionFunc)menu_position_function, editor, 0, event->time);
			return TRUE;
		}

		case GDK_KEY_Return: {
			if (editor->single_line) {
				editor->single_line_return(editor);
			} else {
				dirty_line_update(editor);
				char *r = alloca(sizeof(char) * (editor->buffer->cursor.line->cap + 2));
				if (config_intval(&(editor->buffer->config), CFG_AUTOINDENT)) {
					buffer_indent_newline(editor->buffer, r);
				} else {
					r[0] = '\n';
					r[1] = '\0';
				}
				editor_replace_selection(editor, r);
			}
			return TRUE;
		}
		case GDK_KEY_Escape:
			return TRUE;
		default: // At least one modifier must be pressed to activate special actions
			goto im_context;
		}
	}

	/* Keybindings specially defined for a single-line editor */
	if (editor->single_line) {
		if (editor->single_line_other_keys(editor, shift, ctrl, alt, super, event->keyval)) {
			return TRUE;
		}
	}

	/* Ctrl-Tab for tab insertion */
	if (!shift && ctrl && !alt && !super && (event->keyval == GDK_KEY_Tab)) {
		editor_replace_selection(editor, "\t");
		return TRUE;
	}

	if (shift && !ctrl && !alt && !super) {
		if ((event->keyval >= 0x20) && (event->keyval <= 0x7e)) {
			goto im_context;
		}
	}

	COMPL_WND_HIDE(editor->completer);

	if (strcmp(pressed, "") == 0) {
		full_keyevent_to_string(event->keyval, super, ctrl, alt, shift, pressed);
	}

	if (pressed[0] == '\0') goto im_context;

	command = g_hash_table_lookup(keybindings, pressed);
	//printf("Keybinding [%s] -> {%s}\n", pressed, command);

	if (command != NULL) {
		interp_eval(editor, NULL, command, false);
		goto key_press_return_true;
	}

	return TRUE;

 key_press_return_true:
	buffer_extend_selection_by_select_type(editor->buffer);
	editor_complete_move(editor, true);

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
	if (editor->ignore_next_entry_keyrelease) {
		editor->ignore_next_entry_keyrelease = FALSE;
		return TRUE;
	}

	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	if (!shift && !ctrl && !alt && !super) {
		switch(event->keyval) {
		case GDK_KEY_Escape:
			if (COMPL_WND_VISIBLE(editor->completer)) {
				COMPL_WND_HIDE(editor->completer);
			} else {
				if (editor->single_line) {
					editor->single_line_escape(editor);
				} else {
					top_start_command_line(editor, NULL);
				}
			}
			return TRUE;
		}
	}

	return FALSE;
}

static void absolute_position(editor_t *editor, double *x, double *y) {
	GtkAllocation allocation;
	gtk_widget_get_allocation(editor->drar, &allocation);

	*x += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));
	*y += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
}

static void move_cursor_to_mouse(editor_t *editor, double x, double y) {
	absolute_position(editor, &x, &y);
	buffer_move_cursor_to_position(editor->buffer, x, y);
}

static bool on_file_link(editor_t *editor, double x, double y, lpoint_t *r) {
	absolute_position(editor, &x, &y);
	lpoint_t p;
	buffer_point_from_position(editor->buffer, x, y, &p);

	if (r != NULL) copy_lpoint(r, &p);

	if (p.glyph < p.line->cap) {
		if (p.line->glyph_info[p.glyph].color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING)) {
			return true;
		}
	}

	return false;
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, editor_t *editor) {
	gtk_widget_grab_focus(editor->drar);

	dirty_line_update(editor);

	if (event->button == 1) {
		lpoint_t p;
		if (on_file_link(editor, event->x, event->y, &p)) {
			lpoint_t start, end;
			select_file(editor->buffer, &p, &start, &end);

			char *text = buffer_lines_to_text(editor->buffer, &start, &end);
			const char *cmd = lexy_get_link_fn(editor->buffer);
			const char *argv[] = { cmd, "1", text };

			interp_eval_command(editor, NULL, 3, argv);

			free(text);
		} else {
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
		}
	} else if (event->button == 2) {
		move_cursor_to_mouse(editor, event->x, event->y);
		buffer_unset_mark(editor->buffer);
		editor_complete_move(editor, TRUE);

		gchar *text = gtk_clipboard_wait_for_text(selection_clipboard);
		if (text != NULL) {
			editor_replace_selection(editor, text);
			g_free(text);
		}
	} else if (event->button == 3) {
		lpoint_t start, end;
		buffer_get_selection(editor->buffer, &start, &end);
		if ((start.line != NULL) && (end.line != NULL))
			gtk_menu_popup(GTK_MENU(editor->context_menu), NULL, NULL, NULL, NULL, event->button, event->time);
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
	move_cursor_to_mouse(editor, x+editor->buffer->em_advance, y+editor->buffer->line_height);
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

		allocation.width -= 10;
		allocation.height -= 10;

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
		if (on_file_link(editor, event->x, event->y, NULL)) {
			gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_HAND1));
		} else {
			gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_XTERM));
		}

		end_selection_scroll(editor);
	}

	// focus follows mouse
	if ((config_intval(&(editor->buffer->config), CFG_FOCUS_FOLLOWS_MOUSE)) && focus_can_follow_mouse && !top_command_line_focused()) {
		GtkWidget *requested_focus = (editor->research.mode!=SM_NONE) ? editor->search_entry : editor->drar;
		if (!gtk_widget_is_focus(requested_focus)) {
			gtk_widget_grab_focus(requested_focus);
			gtk_widget_queue_draw(requested_focus);
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

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_SEL_COLOR));

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

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_FG_COLOR));
}

static void draw_parmatch(editor_t *editor, GtkAllocation *allocation, cairo_t *cr) {
	buffer_update_parmatch(editor->buffer);

	if (editor->buffer->parmatch.matched.line == NULL) return;

	double x, y;
	line_get_glyph_coordinates(editor->buffer, &(editor->buffer->parmatch.matched), &x, &y);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_SEL_COLOR));

	cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);

	cairo_rectangle(cr, x, y - editor->buffer->ascent, LPOINTGI(editor->buffer->parmatch.matched).x_advance, editor->buffer->ascent + editor->buffer->descent);
	cairo_fill(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

struct growable_glyph_array {
	cairo_glyph_t *glyphs;
	int n;
	int allocated;

	struct underline_info_t {
		double filey, filex_start, filex_end;
	} *underline_info;

	int underline_n;
	int underline_allocated;

	uint16_t kind;
};

static struct growable_glyph_array *growable_glyph_array_init(void) {
	struct growable_glyph_array *gga = malloc(sizeof(struct growable_glyph_array));
	gga->allocated = 10;
	gga->n = 0;
	gga->kind = 0;
	gga->glyphs = malloc(sizeof(cairo_glyph_t) * gga->allocated);
	alloc_assert(gga->glyphs);

	gga->underline_n = 0;
	gga->underline_allocated = 1;
	gga->underline_info = malloc(sizeof(struct underline_info_t) * gga->underline_allocated);
	alloc_assert(gga->underline_info);

	return gga;
}

static void growable_glyph_array_free(struct growable_glyph_array *gga) {
	free(gga->glyphs);
	free(gga->underline_info);
	free(gga);
}

static void growable_glyph_array_append(struct growable_glyph_array *gga, cairo_glyph_t glyph) {
	if (gga->n >= gga->allocated) {
		gga->allocated *= 2;
		gga->glyphs = realloc(gga->glyphs, sizeof(cairo_glyph_t) * gga->allocated);
		alloc_assert(gga->glyphs);
	}

	gga->glyphs[gga->n] = glyph;
	++(gga->n);
}

static void growable_glyph_array_append_underline(struct growable_glyph_array *gga, double filey, double filex_start, double filex_end) {
	if (gga->underline_n >= gga->underline_allocated) {
		gga->underline_allocated *= 2;
		gga->underline_info = realloc(gga->underline_info, sizeof(struct underline_info_t) * gga->underline_allocated);
		alloc_assert(gga->underline_info);
	}

	gga->underline_info[gga->underline_n].filey = filey;
	gga->underline_info[gga->underline_n].filex_start = filex_start;
	gga->underline_info[gga->underline_n].filex_end = filex_end;
	++(gga->underline_n);
}

#define AUTOWRAP_INDICATOR_WIDTH 2.0
static void draw_line(editor_t *editor, GtkAllocation *allocation, cairo_t *cr, real_line_t *line, GHashTable *ht) {
	struct growable_glyph_array *gga_current = NULL;

	double cury = line->start_y;

	double filey, filex_start, filex_end;
	bool onfile = false;

	for (int i = 0; i < line->cap; ++i) {
		// draws soft wrapping indicators
		if (line->glyph_info[i].y - cury > 0.001) {
			/* draw ending tract */
			cairo_set_line_width(cr, AUTOWRAP_INDICATOR_WIDTH);

			double indy = cury - (editor->buffer->ex_height/2.0);
			cairo_move_to(cr, allocation->width - editor->buffer->right_margin, indy);
			cairo_line_to(cr, allocation->width, indy);

			/*cairo_move_to(cr, line->glyph_info[i-1].x + line->glyph_info[i-1].x_advance, indy);
			cairo_line_to(cr, allocation->width, indy);*/
			cairo_stroke(cr);

			cury = line->glyph_info[i].y;

			/* draw initial tract */
			cairo_set_line_width(cr, AUTOWRAP_INDICATOR_WIDTH);
			cairo_move_to(cr, 0.0, cury-(editor->buffer->ex_height/2.0));
			cairo_line_to(cr, editor->buffer->left_margin, cury-(editor->buffer->ex_height/2.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 2.0);
		}

		uint16_t type = (uint16_t)(line->glyph_info[i].color) + ((uint16_t)(line->glyph_info[i].fontidx) << 8);

		bool thisfile = line->glyph_info[i].color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING);

		if (thisfile) {
			if (onfile && (abs(filey - line->glyph_info[i].y) < 0.001)) {
				// we are still on the same line and still on a file, extend underline
				filex_end = line->glyph_info[i].x + line->glyph_info[i].x_advance;
			} else {
				// either we weren't on a file or we moved to a different line,
				// start a new set of underline information

				if (onfile) {
					assert(gga_current != NULL);
					// we moved to a different line
					growable_glyph_array_append_underline(gga_current, filey, filex_start, filex_end);
				}

				filey = line->glyph_info[i].y;
				filex_start = line->glyph_info[i].x;
				filex_end = line->glyph_info[i].x + line->glyph_info[i].x_advance;
				onfile = true;
			}
		} else {
			if (onfile) {
				assert(gga_current != NULL);
				growable_glyph_array_append_underline(gga_current, filey, filex_start, filex_end);
				onfile = false;
			}
		}

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

	if (onfile && (gga_current != NULL)) {
		growable_glyph_array_append_underline(gga_current, filey, filex_start, filex_end);
	}
}

static void draw_cursorline(cairo_t *cr, editor_t *editor) {
	if (!(editor->cursor_visible)) return;
	if (editor->buffer->cursor.line == NULL) return;
	if (editor->single_line) return;

	GtkAllocation allocation;
	gtk_widget_get_allocation(editor->drar, &allocation);

	double cursor_x, cursor_y;
	buffer_cursor_position(editor->buffer, &cursor_x, &cursor_y);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_BG_CURSORLINE));
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	//printf("Cursor line %g,%g %d - %g | %g\n", cursor_x, cursor_y, allocation.width, editor->buffer->ascent, editor->buffer->descent);

	cairo_rectangle(cr, cursor_x - allocation.width, cursor_y-editor->buffer->ascent, 2*allocation.width, editor->buffer->ascent+editor->buffer->descent);
	cairo_fill(cr);
}

static void draw_underline(editor_t *editor, cairo_t *cr, struct growable_glyph_array *gga) {
	for (int i = 0; i < gga->underline_n; ++i) {
		struct underline_info_t *u = (gga->underline_info + i);
		//cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_rectangle(cr, u->filex_start, u->filey - editor->buffer->underline_position, u->filex_end - u->filex_start, editor->buffer->underline_thickness);
		cairo_fill(cr);
		//printf("Drawing underline from %g to %g at (%g+%g) (thickness: %g)\n", u->filex_start, u->filex_end, u->filey, editor->buffer->underline_position, editor->buffer->underline_thickness);
	}
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, editor_t *editor) {
	cairo_t *cr = gdk_cairo_create(widget->window);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_XTERM));

	if (editor->research.mode == SM_NONE) {
		gtk_widget_hide(editor->search_box);
	}

	if (editor->buffer->stale) {
		gtk_widget_show(editor->stale_box);
	} else {
		gtk_widget_hide(editor->stale_box);
	}

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_BG_COLOR));
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	cairo_translate(cr, -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

	buffer_typeset_maybe(editor->buffer, allocation.width, editor->single_line, false);

	draw_cursorline(cr, editor);

	editor->buffer->rendered_height = 0.0;

	double originy = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	GHashTable *ht = g_hash_table_new(g_direct_hash, g_direct_equal);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_FG_COLOR));

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
			cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config_strval(&(editor->buffer->config), CFG_MAIN_FONT), fontidx));
			set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_LEXY_NOTHING+color));
			cairo_show_glyphs(cr, gga->glyphs, gga->n);
			//set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_LEXY_FILE));
			draw_underline(editor, cr, gga);

			growable_glyph_array_free(gga);
		}
	}

	//printf("\n\n");

	g_hash_table_destroy(ht);

	draw_selection(editor, allocation.width, cr);
	draw_parmatch(editor, &allocation, cr);

	if (editor->cursor_visible && (editor->research.mode == SM_NONE)) {
		double cursor_x, cursor_y;

		set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_FG_COLOR));

		buffer_cursor_position(editor->buffer, &cursor_x, &cursor_y);

		cairo_rectangle(cr, cursor_x, cursor_y-editor->buffer->ascent, 2, editor->buffer->ascent+editor->buffer->descent);
		cairo_fill(cr);
	}

	/********** NOTHING IS TRANSLATED BEYOND THIS ***************************/
	cairo_translate(cr, gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

	if (!editor->single_line) {
		char *posbox_text;
		cairo_text_extents_t posbox_ext;
		double x, y;

		asprintf(&posbox_text, " %d,%d %0.0f%%", editor->buffer->cursor.line->lineno+1, editor->buffer->cursor.glyph+1, (100.0 * editor->buffer->cursor.line->lineno / count));

		cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config_strval(&(editor->buffer->config), CFG_POSBOX_FONT), 0));

		cairo_text_extents(cr, posbox_text, &posbox_ext);

		y = allocation.height - posbox_ext.height - 4.0;
		x = allocation.width - posbox_ext.x_advance - 4.0;

		set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_POSBOX_BORDER_COLOR));
		cairo_rectangle(cr, x-1.0, y-1.0, posbox_ext.x_advance+4.0, posbox_ext.height+4.0);
		cairo_fill(cr);
		set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_POSBOX_BG_COLOR));
		cairo_rectangle(cr, x, y, posbox_ext.x_advance + 2.0, posbox_ext.height + 2.0);
		cairo_fill(cr);

		cairo_move_to(cr, x+1.0, y+posbox_ext.height);
		set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_POSBOX_FG_COLOR));
		cairo_show_text(cr, posbox_text);

		free(posbox_text);
	}

	if (editor->single_line) {
		gtk_widget_hide(GTK_WIDGET(editor->drarhscroll));
		gtk_widget_hide(GTK_WIDGET(editor->drarscroll));
	} else {
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
	if (!editor->single_line) {
		top_show_status();
	}
	if (editor->research.mode != SM_NONE) {
		gtk_widget_grab_focus(editor->search_entry);
	}
	return FALSE;
}

static gboolean editor_focusout_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	COMPL_WND_HIDE(editor->completer);
	editor->cursor_visible = 0;
	gtk_widget_queue_draw(editor->drar);
	end_selection_scroll(editor);
	return FALSE;
}

static gboolean search_key_press_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	if (!shift && !ctrl && !alt && !super) {
		if ((event->keyval == GDK_KEY_Escape) || (event->keyval == GDK_KEY_Return)) {
			quit_search_mode(editor);
			return TRUE;
		}
	}

	if (ctrl && !alt && !super) {
		if (event->keyval == GDK_KEY_g) {
			move_search(editor, true, true, false);
			return TRUE;
		} else if (event->keyval == GDK_KEY_G) {
			move_search(editor, true, false, false);
			return TRUE;
		}
	}

	if (editor->research.mode == SM_REGEXP) {
		if (!shift && !ctrl && !alt && !super) {
			switch (event->keyval) {
			case GDK_KEY_r:
			case GDK_KEY_a:
				move_search(editor, true, true, true);
				break;
			case GDK_KEY_n:
				move_search(editor, true, true, false);
				break;
			case GDK_KEY_q:
				quit_search_mode(editor);
				break;
			}
		}
	}

	return FALSE;
}

static void search_changed_callback(GtkEditable *editable, editor_t *editor) {
	move_search(editor, false, true, false);
}

static void search_button_forward(GtkButton *btn, editor_t *editor) {
	move_search(editor, true, true, false);
}

static void search_button_backwards(GtkButton *btn, editor_t *editor) {
	move_search(editor, true, false, false);
}

static void search_button_close(GtkButton *btn, editor_t *editor) {
	quit_search_mode(editor);
}

static void search_button_execute(GtkButton *btn, editor_t *editor) {
	move_search(editor, true, true, true);
}

static void search_button_execute_all(GtkButton *btn, editor_t *editor) {
	research_continue_replace_to_end(editor);
	editor->research.mode = SM_NONE;
	gtk_widget_grab_focus(editor->drar);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

static void keep_stale_callback(GtkButton *btn, editor_t *editor) {
	editor->buffer->stale = false;
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}

static void reload_stale_callback(GtkButton *btn, editor_t *editor) {
	editor->buffer->modified = false;
	buffers_refresh(editor->buffer);
}

static char *get_selection_or_file_link(editor_t *editor, bool *islink) {
	buffer_t *buffer = editor->buffer;
	lpoint_t start, end;
	buffer_get_selection(editor->buffer, &start, &end);

	*islink = false;

	if ((start.line != NULL) && (end.line != NULL)) return buffer_lines_to_text(editor->buffer, &start, &end);

	if ((buffer->cursor.glyph < buffer->cursor.line->cap) && (buffer->cursor.line->glyph_info[buffer->cursor.glyph].color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING))) {
		select_file(editor->buffer, &(editor->buffer->cursor), &start, &end);
		*islink = true;
		return buffer_lines_to_text(editor->buffer, &start, &end);
	}

	return NULL;
}

static void eval_menu_item_callback(GtkMenuItem *menuitem, editor_t *editor) {
	bool islink;
	char *selection = get_selection_or_file_link(editor, &islink);
	if (selection == NULL) return;

	interp_eval(editor, NULL, selection, true);

	free(selection);
}

static void search_menu_item_callback(GtkMenuItem *menuitem, editor_t *editor) {
	bool islink;
	char *selection = get_selection_or_file_link(editor, &islink);
	if (selection == NULL) return;

	editor_start_search(editor, SM_LITERAL, selection);

	free(selection);
}

static void open_link_menu_item_callback(GtkMenuItem *menuitem, editor_t *editor) {
	bool islink;
	char *selection = get_selection_or_file_link(editor, &islink);
	//printf("Returned selection %s\n", selection);
	if (selection == NULL) return;

	const char *cmd = lexy_get_link_fn(editor->buffer);
	const char *argv[] = { cmd, islink ? "1": "0", selection };

	interp_eval_command(editor, NULL, 3, argv);

	free(selection);
}

static void tag_search_menu_item_callback(GtkMenuItem *menuitem, editor_t *editor) {
	bool islink;
	char *selection = get_selection_or_file_link(editor, &islink);
	if (selection == NULL) return;

	const char *argv[] = { "teddy_intl::tags_search_menu_command", selection };

	interp_eval_command(editor, NULL, 2, argv);

	free(selection);
}

static void copy_to_cmdline_callback(GtkMenuItem *menuitem, editor_t *editor) {
	bool islink;
	char *selection = get_selection_or_file_link(editor, &islink);
	if (selection == NULL) return;

	top_start_command_line(editor, selection);

	free(selection);
}

editor_t *new_editor(buffer_t *buffer, bool single_line) {
	GtkWidget *editor_widget = g_object_new(GTK_TYPE_TEDITOR, NULL);
	editor_t *r = GTK_TEDITOR(editor_widget);

	r->buffer = buffer;
	r->cursor_visible = FALSE;
	r->mouse_marking = 0;
	r->ignore_next_entry_keyrelease = FALSE;
	r->center_on_cursor_after_next_expose = TRUE;
	r->warp_mouse_after_next_expose = FALSE;
	r->completer = &the_generic_word_completer;
	r->dirty_line = false;

	r->single_line_escape = NULL;
	r->single_line_return = NULL;

	r->drar = gtk_drawing_area_new();
	r->drarim = gtk_im_multicontext_new();

	r->selection_scroll_timer = -1;

	r->single_line = single_line;

	research_init(&(r->research));

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


	r->drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(r->adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
	r->drarhscroll = gtk_hscrollbar_new((GtkAdjustment *)(r->hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));

	{ // search box
		r->search_entry = gtk_entry_new();
		r->search_box = gtk_hbox_new(FALSE, 0);
		GtkWidget *search_label = gtk_label_new("Search: ");

		GtkWidget *next_search_button = gtk_button_new();
		r->prev_search_button = gtk_button_new();
		GtkWidget *close_search_button = gtk_button_new();
		r->execute_search_button = gtk_button_new_with_label("Apply");
		r->execute_all_search_button = gtk_button_new_with_label("Apply to all");

		gtk_container_add(GTK_CONTAINER(next_search_button), gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE));
		gtk_container_add(GTK_CONTAINER(r->prev_search_button), gtk_arrow_new(GTK_ARROW_UP, GTK_SHADOW_NONE));
		gtk_container_add(GTK_CONTAINER(close_search_button), gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_SMALL_TOOLBAR));

		g_signal_connect(G_OBJECT(next_search_button), "clicked", G_CALLBACK(search_button_forward), r);
		g_signal_connect(G_OBJECT(r->prev_search_button), "clicked", G_CALLBACK(search_button_backwards), r);
		g_signal_connect(G_OBJECT(close_search_button), "clicked", G_CALLBACK(search_button_close), r);
		g_signal_connect(G_OBJECT(r->execute_search_button), "clicked", G_CALLBACK(search_button_execute), r);
		g_signal_connect(G_OBJECT(r->execute_all_search_button), "clicked", G_CALLBACK(search_button_execute_all), r);

		gtk_box_pack_start(GTK_BOX(r->search_box), GTK_WIDGET(search_label), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(r->search_box), GTK_WIDGET(r->search_entry), TRUE, TRUE, 0);
		gtk_box_pack_end(GTK_BOX(r->search_box), close_search_button, FALSE, FALSE, 2);
		gtk_box_pack_end(GTK_BOX(r->search_box), r->prev_search_button, FALSE, FALSE, 2);
		gtk_box_pack_end(GTK_BOX(r->search_box), next_search_button, FALSE, FALSE, 2);
		gtk_box_pack_end(GTK_BOX(r->search_box), r->execute_all_search_button, FALSE, FALSE, 2);
		gtk_box_pack_end(GTK_BOX(r->search_box), r->execute_search_button, FALSE, FALSE, 2);

		gtk_widget_show_all(r->search_box);
		gtk_widget_set_no_show_all(r->search_box, TRUE);
		gtk_widget_hide(r->search_box);
	}

	{ // stale box
		r->stale_box = gtk_hbox_new(FALSE, 0);
		GtkWidget *stale_label = gtk_label_new("Content of this file changed on disk");
		GtkWidget *keep_stale_button = gtk_button_new_with_label("Keep this version");
		GtkWidget *reload_stale_button = gtk_button_new_with_label("Reload from disk");

		gtk_box_pack_start(GTK_BOX(r->stale_box), stale_label, TRUE, TRUE, 0);
		gtk_box_pack_end(GTK_BOX(r->stale_box), keep_stale_button, FALSE, FALSE, 2);
		gtk_box_pack_end(GTK_BOX(r->stale_box), reload_stale_button, FALSE, FALSE, 2);

		g_signal_connect(G_OBJECT(keep_stale_button), "clicked", G_CALLBACK(keep_stale_callback), r);
		g_signal_connect(G_OBJECT(reload_stale_button), "clicked", G_CALLBACK(reload_stale_callback), r);

		gtk_widget_show_all(r->stale_box);
		gtk_widget_set_no_show_all(r->stale_box, TRUE);
		gtk_widget_hide(r->stale_box);
	}

	gtk_table_attach(GTK_TABLE(r), r->stale_box, 0, 2, 0, 1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(r), r->search_box, 0, 2, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(r), r->drar, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(r), r->drarscroll, 1, 2, 2, 3, 0, GTK_EXPAND|GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(r), r->drarhscroll, 0, 1, 3, 4, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	g_signal_connect(G_OBJECT(r->drarscroll), "value_changed", G_CALLBACK(scrolled_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(r->drarhscroll), "value_changed", G_CALLBACK(hscrolled_callback), (gpointer)r);

	if (r->single_line) {
		gtk_widget_set_size_request(r->drar, 1, r->buffer->line_height);
	}

	g_signal_connect(G_OBJECT(r->search_entry), "key-press-event", G_CALLBACK(search_key_press_callback), r);
	g_signal_connect(G_OBJECT(r->search_entry), "changed", G_CALLBACK(search_changed_callback), r);

	r->context_menu = gtk_menu_new();

	GtkWidget *eval_menu_item = gtk_menu_item_new_with_label("Eval");
	gtk_menu_append(GTK_MENU(r->context_menu), eval_menu_item);

	GtkWidget *copy_to_cmdline_mitem = gtk_menu_item_new_with_label("Copy text to command line");
	gtk_menu_append(GTK_MENU(r->context_menu), copy_to_cmdline_mitem);

	GtkWidget *open_link_menu_item = gtk_menu_item_new_with_label("Open selected text");
	gtk_menu_append(GTK_MENU(r->context_menu), open_link_menu_item);

	GtkWidget *search_menu_item = gtk_menu_item_new_with_label("Search text");
	gtk_menu_append(GTK_MENU(r->context_menu), search_menu_item);

	GtkWidget *tag_search_menu_item = gtk_menu_item_new_with_label("Search in TAGS");
	gtk_menu_append(GTK_MENU(r->context_menu), tag_search_menu_item);

	gtk_widget_show_all(r->context_menu);

	g_signal_connect(G_OBJECT(eval_menu_item), "activate", G_CALLBACK(eval_menu_item_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(copy_to_cmdline_mitem), "activate", G_CALLBACK(copy_to_cmdline_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(search_menu_item), "activate", G_CALLBACK(search_menu_item_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(open_link_menu_item), "activate", G_CALLBACK(open_link_menu_item_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(tag_search_menu_item), "activate", G_CALLBACK(tag_search_menu_item_callback), (gpointer)r);

	return r;
}

void editor_grab_focus(editor_t *editor, bool warp) {
	//printf("Grabbing focus %p %d %d\n", editor, warp, config_intval(&(editor->buffer->config), CFG_WARP_MOUSE));
	gtk_widget_grab_focus(editor->drar);

	column_t *col;
	tframe_t *frame;
	find_editor_for_buffer(editor->buffer, &col, &frame, NULL);
	if ((col != NULL) && (frame != NULL)) column_expand_frame(col, frame);

	if (config_intval(&(editor->buffer->config), CFG_WARP_MOUSE) && warp) {
		GdkDisplay *display = gdk_display_get_default();
		GdkScreen *screen = gdk_display_get_default_screen(display);
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		//printf("allocation: %d %d\n", allocation.x, allocation.y);
		if ((allocation.x < 0) || (allocation.y < 0)) {
			editor->warp_mouse_after_next_expose = TRUE;
		} else {
			gint wpos_x, wpos_y;
			gdk_window_get_position(gtk_widget_get_window(GTK_WIDGET(columnset)), &wpos_x, &wpos_y);

			//printf("Moving mouse %d+%d %d+%d %d\n", allocation.x, wpos_x, allocation.y, wpos_y, gtk_widget_get_mapped(GTK_WIDGET(editor)));
			gdk_display_warp_pointer(display, screen, allocation.x+wpos_x+5, allocation.y+wpos_y+5);
		}
	}

	if (editor->research.mode) {
		gtk_widget_grab_focus(editor->search_entry);
	}
}

void editor_start_search(editor_t *editor, enum search_mode_t search_mode, const char *initial_search_term) {
	if (editor->single_line) return;

	editor->buffer->mark.line = editor->buffer->cursor.line;
	editor->buffer->mark.glyph = editor->buffer->cursor.glyph;

	editor->research.search_failed = false;
	editor->research.mode = search_mode;

	gtk_widget_grab_focus(editor->search_entry);

	gtk_entry_set_text(GTK_ENTRY(editor->search_entry), (initial_search_term != NULL) ? initial_search_term : "");

	gtk_widget_show(editor->search_box);

	if (editor->research.mode == SM_LITERAL) {
		gtk_widget_hide(editor->execute_search_button);
		gtk_widget_hide(editor->execute_all_search_button);
		gtk_widget_show_all(editor->prev_search_button);
		gtk_entry_set_editable(GTK_ENTRY(editor->search_entry), true);
	} else {
		gtk_widget_hide(editor->prev_search_button);
		if (editor->research.cmd != NULL) {
			gtk_widget_show_all(editor->execute_search_button);
			gtk_widget_show_all(editor->execute_all_search_button);
		} else {
			gtk_widget_hide(editor->execute_search_button);
			gtk_widget_hide(editor->execute_all_search_button);
		}
		gtk_entry_set_editable(GTK_ENTRY(editor->search_entry), false);
	}

	move_search(editor, false, true, false);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}
