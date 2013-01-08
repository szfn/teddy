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

void set_label_text(editor_t *editor) {
	tframe_t *frame;
	find_editor_for_buffer(editor->buffer, NULL, &frame, NULL);

	if (frame != NULL) {
		tframe_set_title(frame, editor->buffer->path);
		tframe_set_wd(frame, editor->buffer->wd);
		tframe_set_modified(frame, editor->buffer->modified);
		gtk_widget_queue_draw(GTK_WIDGET(frame));
	}
}

static void dirty_line_update(editor_t *editor) {
	if (editor->dirty_line) {
		lexy_update_resume(editor->buffer);
		editor->dirty_line = false;
		buffer_wordcompl_update(editor->buffer, &(editor->buffer->cbt), WORDCOMPL_UPDATE_RADIUS);
		word_completer_full_update();
	}
}

static bool editor_maybe_show_completions(editor_t *editor, struct completer *completer, bool autoinsert, int min);

static void editor_replace_selection(editor_t *editor, const char *new_text) {
	buffer_replace_selection(editor->buffer, new_text);

	column_t *column;
	if (find_editor_for_buffer(editor->buffer, &column, NULL, NULL))
		columns_set_active(columnset, column);

	set_label_text(editor);
	gtk_widget_queue_draw(editor->drar);

	editor->dirty_line = true;

	if (compl_wnd_visible(editor->completer)) {
		editor_maybe_show_completions(editor, editor->completer, false, 2);
	} else if (compl_wnd_visible(editor->alt_completer)) {
		editor_maybe_show_completions(editor, editor->alt_completer, false, 0);
	} else if (config_intval(&(editor->buffer->config), CFG_AUTOCOMPL_POPUP) && (strcmp(new_text, "") != 0)) {
		editor_maybe_show_completions(editor, editor->completer, false, 2);
	}

	editor_include_cursor(editor, ICM_TOP, ICM_BOT);
	editor->lineno = buffer_line_of(editor->buffer, editor->buffer->cursor);
	editor->colno = buffer_column_of(editor->buffer, editor->buffer->cursor);
}

static bool editor_maybe_show_completions(editor_t *editor, struct completer *completer, bool autoinsert, int min) {
	char *prefix = completer->prefix_from_buffer(editor->buffer);

	if ((prefix == NULL) || (strlen(prefix) <= min)) {
		compl_wnd_hide(completer);
		return false;
	}

	char *completion = compl_complete(completer, prefix);

	if (completion != NULL) {
		bool empty_completion = strcmp(completion, "") == 0;

		if (autoinsert && !empty_completion) editor_replace_selection(editor, completion);
		double x, y, alty;
		editor_cursor_position(editor, &x, &y, &alty);

		if (empty_completion || !autoinsert) {
			compl_wnd_show(completer, prefix, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)), false, false);
		} else {
			char *new_prefix;
			asprintf(&new_prefix, "%s%s", prefix, completion);
			alloc_assert(new_prefix);
			compl_wnd_show(completer, new_prefix, x, y, alty, gtk_widget_get_toplevel(GTK_WIDGET(editor)), false, false);
			free(new_prefix);
		}
		free(completion);
	} else {
		compl_wnd_hide(completer);
	}

	free(prefix);

	return true;
}

/*
Converts a pair of translated coordinates into un-translated coordinates
*/
static void absolute_position(editor_t *editor, double *x, double *y) {
	*x += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));
	*y += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
}

/*
Returns cursor position as a pair of translated coordinates relative to the window
*/
void editor_cursor_position(editor_t *editor, double *x, double *y, double *alty) {
	line_get_glyph_coordinates(editor->buffer, editor->buffer->cursor, x, y);
	*y -= gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
	*x -= gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));

	GtkAllocation allocation;
	gtk_widget_get_allocation(editor->drar, &allocation);
	*x += allocation.x; *y += allocation.y;

	gint wpos_x, wpos_y;
	gdk_window_get_position(gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(editor))), &wpos_x, &wpos_y);
	*x += wpos_x; *y += wpos_y;

	*alty = *y - editor->buffer->line_height;
}

void editor_include_cursor(editor_t *editor, enum include_cursor_mode above, enum include_cursor_mode below) {
	double x, y;
	line_get_glyph_coordinates(editor->buffer, editor->buffer->cursor, &x, &y);
	double ty = y - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
	double tx = x - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));

	GtkAllocation allocation;
	gtk_widget_get_allocation(editor->drar, &allocation);

	my_glyph_info_t *glyph = bat(editor->buffer, editor->buffer->cursor);
	if ((glyph == NULL) || (glyph->code == '\n')) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), 0);
	} else {
		if ((tx - editor->buffer->em_advance) < 0) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), x - editor->buffer->em_advance);
		} else if (tx > allocation.width) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->hadjustment), x - allocation.width);
		}
	}

	double pos_off[] = { editor->buffer->line_height, allocation.height/2, allocation.height };

	if (editor->buffer->cursor < 0) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), 0);
	} else {
		if ((ty - editor->buffer->line_height) < 0) {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), y - pos_off[above]);
		} else if (ty > allocation.height)  {
			gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), y - pos_off[below]);
		}
	}
}

static void editor_get_primary_selection(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, editor_t *editor) {
	char *r = buffer_get_selection_text(editor->buffer);
	if (r != NULL) {
		gtk_selection_data_set_text(selection_data, r, -1);
		free(r);
	}
}

void editor_complete_move(editor_t *editor, gboolean should_move_origin) {
	compl_wnd_hide(editor->completer);
	compl_wnd_hide(editor->alt_completer);
	gtk_widget_queue_draw(editor->drar);
	editor->cursor_visible = TRUE;
	if (should_move_origin) {
		editor_include_cursor(editor, ICM_MID, ICM_MID);
	}
	editor->lineno = buffer_line_of(editor->buffer, editor->buffer->cursor);
	editor->colno = buffer_column_of(editor->buffer, editor->buffer->cursor);
}

static void text_entry_callback(GtkIMContext *context, gchar *str, editor_t *editor) {
	if (editor->research.mode == SM_NONE) {
		editor_replace_selection(editor, str);
	} else if (editor->research.mode == SM_LITERAL) {
		if (editor->research.literal_text_cap + strlen(str) >= editor->research.literal_text_allocated) {
			editor->research.literal_text_allocated *= 2;
			editor->research.literal_text = realloc(editor->research.literal_text, sizeof(uint32_t) * editor->research.literal_text_allocated);
			alloc_assert(editor->research.literal_text);
		}

		for (int src = 0; src < strlen(str); ) {
			bool valid = false;
			uint32_t code = utf8_to_utf32(str, &src, strlen(str), &valid);
			if (!valid) ++src;
			else {
				editor->research.literal_text[editor->research.literal_text_cap] = code;
				++(editor->research.literal_text_cap);
			}
		}

		move_search(editor, false, true, false);
	}
}

void editor_switch_buffer(editor_t *editor, buffer_t *buffer) {
	editor->buffer = buffer;
	set_label_text(editor);

	{
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		buffer_typeset_maybe(editor->buffer, allocation.width, false);
	}

	editor->lineno = buffer_line_of(buffer, buffer->cursor);
	editor->colno = buffer_column_of(buffer, buffer->cursor);
	editor->center_on_cursor_after_next_expose = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(editor));
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
	if (editor->buffer->mark >= 0) {
		gtk_clipboard_set_with_data(selection_clipboard, &selection_clipboard_target_entry, 1, (GtkClipboardGetFunc)editor_get_primary_selection, NULL, editor);
	}
}

static void freeze_primary_selection(editor_t *editor) {
	char *r = buffer_get_selection_text(editor->buffer);
	if (r == NULL) return;
	if (strcmp(r, "") != 0) gtk_clipboard_set_text(selection_clipboard, r, -1);
	free(r);
}

static bool mark_move(editor_t *editor, bool shift) {
	if (editor->buffer->mark < 0) {
		if (shift) {
			editor->buffer->savedmark = editor->buffer->mark = editor->buffer->cursor;
			editor->buffer->select_type = BST_NORMAL;
			set_primary_selection(editor);
		}
		return true;
	} else {
		if (!shift) {
			//freeze_primary_selection(editor);
			editor->buffer->mark = editor->buffer->savedmark = -1;
			return false;
		} else {
			return true;
		}
	}
}

void editor_save_action(editor_t *editor) {
	save_to_text_file(editor->buffer);
	set_label_text(editor);
	save_tied_session();
}

void editor_undo_action(editor_t *editor, bool redo) {
	buffer_undo(editor->buffer, redo);

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

static void editor_complete(editor_t *editor, struct completer *completer) {
	char *completion = compl_wnd_get(completer, false);
	compl_wnd_hide(completer);
	if (completion != NULL) {
		editor_replace_selection(editor, completion);
		free(completion);
	}
}

static void menu_position_function(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, editor_t *editor) {
	*push_in = TRUE;

	double dx, dy, alty;
	editor_cursor_position(editor, &dx, &dy, &alty);
	*x = (int)dx;
	*y = (int)dy;
}

static char *select_file(buffer_t *buffer, int p) {
	int start = p, end = p;
	for (; start > 0; --start)
		if (bat(buffer, start)->color != CFG_LEXY_FILE - CFG_LEXY_NOTHING) {
			++start;
			break;
		}

	for (; end < BSIZE(buffer); ++end)
		if (bat(buffer, end)->color != CFG_LEXY_FILE - CFG_LEXY_NOTHING) break;

	return buffer_lines_to_text(buffer, start, end);
}

static struct completer *editor_visible_completer(editor_t *editor) {
	if (compl_wnd_visible(editor->completer)) return editor->completer;
	if (compl_wnd_visible(editor->alt_completer)) return editor->alt_completer;
	return NULL;
}

static const char *indentchar(editor_t *editor) {
	const char *propvalue = g_hash_table_lookup(editor->buffer->props, "indentchar");
	return (propvalue != NULL) ? propvalue : "\t";
}

static bool search_key_press_callback(editor_t *editor, GdkEventKey *event) {
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	if (!shift && !ctrl && !alt && !super) {
		if ((event->keyval == GDK_KEY_Escape) || (event->keyval == GDK_KEY_Return)) {
			quit_search_mode(editor);
			return true;
		}
	}

	if (ctrl && !alt && !super) {
		if (event->keyval == GDK_KEY_g) {
			move_search(editor, true, true, false);
			return true;
		} else if (event->keyval == GDK_KEY_G) {
			move_search(editor, true, false, false);
			return true;
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
			case GDK_KEY_A:
				research_continue_replace_to_end(editor);
				editor->research.mode = SM_NONE;
				gtk_widget_grab_focus(editor->drar);
				gtk_widget_queue_draw(GTK_WIDGET(editor));
				break;
			}
		}
		return true;
	} else if (editor->research.mode == SM_LITERAL) {
		if (event->keyval == GDK_KEY_BackSpace) {
			if (editor->research.literal_text_cap > 0) {
				--(editor->research.literal_text_cap);
				if ((editor->buffer->mark >= 0) && (editor->buffer->cursor != editor->buffer->mark)) {
					--(editor->buffer->cursor);
					gtk_widget_queue_draw(editor->drar);
				}
			}
		}
	}

	return false;
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	char pressed[40] = "";
	const char *command;
	GtkAllocation allocation;
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	gdk_window_set_cursor(gtk_widget_get_window(editor->drar), cursor_blank);

	if (editor->buffer->stale) {
		switch (event->keyval) {
		case GDK_KEY_y:
			editor->buffer->modified = false;
			buffers_refresh(editor->buffer);
			break;
		case GDK_KEY_n:
			editor->buffer->stale = false;
			gtk_widget_queue_draw(GTK_WIDGET(editor));
			break;
		default:
			// Do nothing
			break;
		}
		return TRUE;
	}

	if (editor->research.mode != SM_NONE) {
		if (search_key_press_callback(editor, event)) {
			return TRUE;
		} else {
			goto im_context;
		}
	}

	gtk_widget_get_allocation(editor->drar, &allocation);

	if (editor->buffer->keyprocessor != NULL) {
		full_keyevent_to_string(event->keyval, super, ctrl, alt, shift, pressed);
		if (pressed[0] != '\0') {
			compl_wnd_hide(editor->completer);
			compl_wnd_hide(editor->alt_completer);
			const char *eval_argv[] = { editor->buffer->keyprocessor, pressed };
			const char *r = interp_eval_command(editor, NULL, 2, eval_argv);

			if ((r != NULL) && (strcmp(r, "done") == 0)) return FALSE;
		}
	}

	/* Manipulation of completions window */
	if (!shift && !ctrl && !alt && !super) {
		struct completer *visible_completer = editor_visible_completer(editor);
		if (visible_completer != NULL) {
			switch(event->keyval) {
				case GDK_KEY_Tab:
					if (visible_completer->common_suffix != NULL) {
						editor_replace_selection(editor, visible_completer->common_suffix);
					} else {
						compl_wnd_down(visible_completer);
					}
					return TRUE;
				case GDK_KEY_Escape:
					return FALSE;
				case GDK_KEY_Insert:
					editor_complete(editor, visible_completer);
					return TRUE;
				case GDK_KEY_Return:
					if (config_intval(&(editor->buffer->config), CFG_AUTOCOMPL_POPUP) == 0) {
						// only if we don't automatically popup the autocompletions window then return will select the current completion
						editor_complete(editor, visible_completer);
						return TRUE;
					}
					break;
			}
		} else if (editor->buffer->single_line) {
			if (editor->single_line_other_keys(editor, shift, ctrl, alt, super, event->keyval)) {
				return TRUE;
			}
		}
	}

	/* Motion and keys that are invariant to shift */
	if (!ctrl && !alt && !super) {
		switch(event->keyval) {
		case GDK_KEY_Delete:
			if (editor->buffer->mark < 0) {
				editor->buffer->mark = editor->buffer->cursor;
				buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_REL, +1);
			}
			editor_replace_selection(editor, "");
			return TRUE;

		case GDK_KEY_BackSpace:
			if (editor->buffer->mark < 0) {
				editor->buffer->mark = editor->buffer->cursor;
				buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_REL, -1);
			}
			editor_replace_selection(editor, "");
			return TRUE;

		case GDK_KEY_Up:
			dirty_line_update(editor);
			if (mark_move(editor, shift)) {
				buffer_move_point_line(editor->buffer, &(editor->buffer->cursor), MT_REL, -1);
				buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_START, 0);
			}
			goto key_press_return_true;
		case GDK_KEY_Down:
			dirty_line_update(editor);
			if (mark_move(editor, shift)) {
				buffer_move_point_line(editor->buffer, &(editor->buffer->cursor), MT_REL, +1);
				buffer_move_point_glyph(editor->buffer, &(editor->buffer->cursor), MT_START, 0);
			}
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
			struct completer *c = editor_visible_completer(editor);
			if (c == NULL) c = editor->completer;
			if (!editor_maybe_show_completions(editor, c, true, 0)) {
				editor_replace_selection(editor, indentchar(editor));
			}

			return TRUE;
		}

		case GDK_KEY_Menu: {
			if ((editor->buffer->mark >= 0) || ((editor->buffer->cursor >= 0) && (bat(editor->buffer, editor->buffer->cursor)->color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING))))
				gtk_menu_popup(GTK_MENU(editor->context_menu), NULL, NULL, (GtkMenuPositionFunc)menu_position_function, editor, 0, event->time);
			return TRUE;
		}

		case GDK_KEY_Return: {
			compl_wnd_hide(editor->completer);
			compl_wnd_hide(editor->alt_completer);
			if (editor->buffer->single_line) {
				editor->single_line_return(editor);
			} else {
				dirty_line_update(editor);

				if ((editor->buffer->job != NULL) && (editor->buffer->cursor == BSIZE(editor->buffer))) {
					// send input
					job_send_input(editor->buffer->job);
				} else if (config_intval(&(editor->buffer->config), CFG_AUTOINDENT)) {
					char *r = buffer_indent_newline(editor->buffer);
					editor_replace_selection(editor, r);
					free(r);
				} else {
					editor_replace_selection(editor, "\n");
				}
			}
			return TRUE;
		}
		case GDK_KEY_Escape:
			return TRUE;

		// Special keys that can be bound
		case GDK_KEY_Insert:
		case GDK_KEY_F1:
		case GDK_KEY_F2:
		case GDK_KEY_F3:
		case GDK_KEY_F4:
		case GDK_KEY_F5:
		case GDK_KEY_F6:
		case GDK_KEY_F7:
		case GDK_KEY_F8:
		case GDK_KEY_F9:
		case GDK_KEY_F10:
		case GDK_KEY_F11:
		case GDK_KEY_F12:
		case GDK_KEY_F13:
		case GDK_KEY_F14:
			break;

		default: // At least one modifier must be pressed to activate special actions (unless it's a special key)
			goto im_context;
		}
	}

	/* Keybindings specially defined for a single-line editor */
	if (editor->buffer->single_line) {
		if (editor->single_line_other_keys(editor, shift, ctrl, alt, super, event->keyval)) {
			return TRUE;
		}
	}

	/* Ctrl-Tab for tab insertion */
	if (!shift && ctrl && !alt && !super && (event->keyval == GDK_KEY_Tab)) {
		editor_replace_selection(editor, indentchar(editor));
		return TRUE;
	}

	if (shift && !ctrl && !alt && !super) {
		if ((event->keyval >= 0x20) && (event->keyval <= 0x7e)) {
			goto im_context;
		}
	}

	compl_wnd_hide(editor->completer);
	compl_wnd_hide(editor->alt_completer);

	if (strcmp(pressed, "") == 0) {
		full_keyevent_to_string(event->keyval, super, ctrl, alt, shift, pressed);
	}

	if (pressed[0] == '\0') goto im_context;

	command = g_hash_table_lookup(keybindings, pressed);

	if (command != NULL) {
		interp_eval(editor, NULL, command, false);
		set_label_text(editor);
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
			if (compl_wnd_visible(editor->completer)) {
				compl_wnd_hide(editor->completer);
			} else if (compl_wnd_visible(editor->alt_completer)) {
				compl_wnd_hide(editor->alt_completer);
			} else {
				if (editor->buffer->single_line) {
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

static void move_cursor_to_mouse(editor_t *editor, double x, double y) {
	absolute_position(editor, &x, &y);
	editor->buffer->cursor = buffer_point_from_position(editor->buffer, 0, x, y, false);
	buffer_extend_selection_by_select_type(editor->buffer);
	//buffer_move_cursor_to_position(editor->buffer, x, y);
}

static bool on_file_link(editor_t *editor, double x, double y, int *r) {
	absolute_position(editor, &x, &y);
	int p = buffer_point_from_position(editor->buffer, editor->first_exposed, x, y, true);

	if (r != NULL) *r = p;

	my_glyph_info_t *glyph = bat(editor->buffer, p);
	if (glyph != NULL) {
		if (glyph->color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING)) {
			return true;
		}
	}

	return false;
}

static void doubleclick_behaviour(editor_t *editor) {
	int m = parmatch_find(editor->buffer, editor->buffer->cursor, -1, true);
	if (m >= 0) {
		editor->buffer->mark = m+1;
		return;
	}
	m = parmatch_find(editor->buffer, editor->buffer->cursor-1, -1, true);
	if (m >= 0) {
		editor->buffer->mark = m;
		return;
	}
	buffer_change_select_type(editor->buffer, BST_WORDS);
	set_primary_selection(editor);
}

static char *get_selection_or_file_link(editor_t *editor, bool *islink) {
	*islink = false;

	char *r = buffer_get_selection_text(editor->buffer);
	if (r != NULL) return r;

	my_glyph_info_t *glyph = bat(editor->buffer, editor->buffer->cursor);

	if ((glyph != NULL) && (glyph->color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING))) {
		char *r = select_file(editor->buffer, editor->buffer->cursor);
		*islink = true;
		return r;
	}

	return NULL;
}

static void eval_menu_item_callback(GtkMenuItem *menuitem, editor_t *editor) {
	bool islink;
	char *selection = get_selection_or_file_link(editor, &islink);
	if (selection == NULL) return;

	char *pdir = get_current_dir_name();
	char *dir = buffer_directory(editor->buffer);
	if (dir != NULL) chdir(dir);

	change_directory_back_after_eval = true;

	interp_eval(editor, NULL, selection, true);

	if (change_directory_back_after_eval) chdir(pdir);
	free(pdir);
	if (dir != NULL) free(dir);

	free(selection);
}

static void open_link(editor_t *editor, bool islink, char *text) {
	const char *cmd = lexy_get_link_fn(editor->buffer);
	const char *argv[] = { cmd, islink ? "1": "0", text };

	char *pdir = get_current_dir_name();
	char *dir = buffer_directory(editor->buffer);
	if (dir != NULL) chdir(dir);

	change_directory_back_after_eval = true;

	interp_eval_command(editor, NULL, 3, argv);

	if (change_directory_back_after_eval) chdir(pdir);
	free(pdir);
	if (dir != NULL) free(dir);
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, editor_t *editor) {
	gtk_widget_grab_focus(editor->drar);

	dirty_line_update(editor);

	if (editor->mouse_marking) {
		if (event->button == 3) {
			editor->mouse_marking = 0;
			eval_menu_item_callback(NULL, editor);
			return TRUE;
		}
	}

	if (event->button == 1) {
		move_cursor_to_mouse(editor, event->x, event->y);

		editor->mouse_marking = 1;
		editor->buffer->savedmark = editor->buffer->mark = editor->buffer->cursor;
		editor->buffer->select_type = BST_NORMAL;

		if (event->type == GDK_2BUTTON_PRESS) {
			doubleclick_behaviour(editor);
		} else if (event->type == GDK_3BUTTON_PRESS) {
			buffer_change_select_type(editor->buffer, BST_LINES);
			set_primary_selection(editor);
		}

		editor_complete_move(editor, TRUE);

		int p;
		if (on_file_link(editor, event->x, event->y, &p)) {
			char *text = select_file(editor->buffer, p);
			open_link(editor, true, text);
			free(text);
			// there was a center on cursor here
		}
	} else if (event->button == 2) {
		move_cursor_to_mouse(editor, event->x, event->y);
		editor->buffer->mark = editor->buffer->savedmark = -1;
		editor_complete_move(editor, TRUE);

		gchar *text = gtk_clipboard_wait_for_text(selection_clipboard);
		if (text != NULL) {
			editor_replace_selection(editor, text);
			g_free(text);
		}
	} else if (event->button == 3) {
		if (editor->buffer->mark >= 0)
			gtk_menu_popup(GTK_MENU(editor->context_menu), NULL, NULL, NULL, NULL, event->button, event->time);
	}

	return TRUE;
}

static gboolean button_release_callback(GtkWidget *widget, GdkEventButton *event, editor_t *editor) {
	editor->buffer->select_type = BST_NORMAL;
	if (editor->mouse_marking) {
		editor->mouse_marking = 0;

		if (editor->buffer->mark == editor->buffer->cursor) {
			editor->buffer->mark = -1;
			gtk_widget_queue_draw(editor->drar);
		} else {
			freeze_primary_selection(editor);
		}
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

	lexy_update_resume(editor->buffer);

	return TRUE;
}

static void selection_move(editor_t *editor, double x, double y) {
	move_cursor_to_mouse(editor, x, y);
	gtk_clipboard_set_with_data(selection_clipboard, &selection_clipboard_target_entry, 1, (GtkClipboardGetFunc)editor_get_primary_selection, NULL, editor);
	editor_include_cursor(editor, ICM_TOP, ICM_BOT);
	gtk_widget_queue_draw(editor->drar);
}

static gboolean selection_autoscroll(editor_t *editor) {
	gint x, y;
	gtk_widget_get_pointer(editor->drar, &x, &y);
	selection_move(editor, x+editor->buffer->em_advance, y+editor->buffer->line_height);
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

		if (inside_allocation(x, y, &allocation)) {
			end_selection_scroll(editor);
			selection_move(editor, event->x, event->y);
		} else {
			start_selection_scroll(editor);
		}

		set_primary_selection(editor);
	} else {
		if (on_file_link(editor, event->x, event->y, NULL)) {
			gdk_window_set_cursor(gtk_widget_get_window(editor->drar), cursor_hand);
		} else if (config_intval(&(editor->buffer->config), CFG_OLDARROW)) {
			gdk_window_set_cursor(gtk_widget_get_window(editor->drar), cursor_arrow);
		} else {
			gdk_window_set_cursor(gtk_widget_get_window(editor->drar), cursor_xterm);
		}

		end_selection_scroll(editor);
	}

	// focus follows mouse
	if ((config_intval(&(editor->buffer->config), CFG_FOCUS_FOLLOWS_MOUSE)) && focus_can_follow_mouse && !top_command_line_focused()) {
		if (!gtk_widget_is_focus(editor->drar)) {
			gtk_widget_grab_focus(editor->drar);
			gtk_widget_queue_draw(editor->drar);
		}
	}

	return TRUE;
}

static void draw_selection(editor_t *editor, double width, cairo_t *cr, int sel_invert) {
	int start, end;
	double selstart_y, selend_y;
	double selstart_x, selend_x;

	if (editor->buffer->mark < 0) return;

	buffer_get_selection(editor->buffer, &start, &end);

	if (start == end) return;

	line_get_glyph_coordinates(editor->buffer, start, &selstart_x, &selstart_y);
	line_get_glyph_coordinates(editor->buffer, end, &selend_x, &selend_y);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_SEL_COLOR));

	cairo_set_operator(cr, sel_invert ? CAIRO_OPERATOR_DIFFERENCE : CAIRO_OPERATOR_OVER);
	if (fabs(selstart_y - selend_y) < 0.001) {
		cairo_rectangle(cr, selstart_x, selstart_y-editor->buffer->ascent, selend_x - selstart_x, editor->buffer->ascent + editor->buffer->descent);
		cairo_fill(cr);
	} else {
		// start of selection
		cairo_rectangle(cr, selstart_x, selstart_y-editor->buffer->ascent, width - selstart_x, editor->buffer->ascent + editor->buffer->descent);
		cairo_fill(cr);

		// middle of selection
		cairo_rectangle(cr, 0.0, selstart_y + editor->buffer->descent, width, selend_y - editor->buffer->ascent - editor->buffer->descent - selstart_y);
		cairo_fill(cr);

		// end of selection
		cairo_rectangle(cr, 0.0, selend_y-editor->buffer->ascent, selend_x, editor->buffer->ascent + editor->buffer->descent);
		cairo_fill(cr);
	}
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_FG_COLOR));
}

static void draw_parmatch(editor_t *editor, GtkAllocation *allocation, cairo_t *cr) {
	int match = parmatch_find(editor->buffer, editor->buffer->cursor, allocation->height / editor->buffer->line_height, false);

	if (match < 0) return;

	double x, y;
	line_get_glyph_coordinates(editor->buffer, match, &x, &y);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_SEL_COLOR));

	cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);

	cairo_rectangle(cr, x, y - editor->buffer->ascent, bat(editor->buffer, match)->x_advance, editor->buffer->ascent + editor->buffer->descent);
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
static void draw_lines(editor_t *editor, GtkAllocation *allocation, cairo_t *cr, GHashTable *ht, double starty, double endy) {
	struct growable_glyph_array *gga_current = NULL;

	my_glyph_info_t *glyph = bat(editor->buffer, 0);

	double cury = (glyph != NULL) ? glyph->y : 0.0;

	double filey, filex_start, filex_end;
	bool onfile = false;

	bool do_underline = config_intval(&(editor->buffer->config), CFG_UNDERLINE_LINKS) != 0;
	bool newline = false;

	editor->first_exposed = -1;

	for  (int i = 0; i < BSIZE(editor->buffer); ++i) {
		my_glyph_info_t *glyph = bat(editor->buffer, i);

		if (glyph->y < starty) {
			newline = (glyph->code == '\n'); // next loop iteration don't draw autowrap indicators
			continue;
		}
		if (glyph->y - editor->buffer->line_height > endy) break;

		if (editor->first_exposed < 0) editor->first_exposed = i;

		// draws soft wrapping indicators
		if (glyph->y - cury > 0.001) {
			if (!newline) {
				/* draw ending tract */
				cairo_set_line_width(cr, AUTOWRAP_INDICATOR_WIDTH);
				double indy = cury - (editor->buffer->ex_height/2.0);
				cairo_move_to(cr, allocation->width - editor->buffer->right_margin, indy);
				cairo_line_to(cr, allocation->width, indy);
				cairo_stroke(cr);
			}

			cury = glyph->y;

			if (!newline) {
				/* draw initial tract */
				cairo_set_line_width(cr, AUTOWRAP_INDICATOR_WIDTH);
				double indy = cury  - (editor->buffer->ex_height/2.0);
				cairo_move_to(cr, 0.0, indy);
				cairo_line_to(cr, editor->buffer->left_margin, indy);
				cairo_stroke(cr);
			}

			cairo_set_line_width(cr, 2.0);
		}

		newline = (glyph->code == '\n'); // next loop iteration don't draw autowrap indicators

		uint16_t type = (uint16_t)(glyph->color) + ((uint16_t)(glyph->fontidx) << 8);

		bool thisfile = glyph->color == (CFG_LEXY_FILE - CFG_LEXY_NOTHING);
		if (!do_underline) thisfile = false;

		if (thisfile) {
			if (onfile && (abs(filey - glyph->y) < 0.001)) {
				// we are still on the same line and still on a file, extend underline
				filex_end = glyph->x + glyph->x_advance;
			} else {
				// either we weren't on a file or we moved to a different line,
				// start a new set of underline information

				if (onfile) {
					assert(gga_current != NULL);
					// we moved to a different line
					growable_glyph_array_append_underline(gga_current, filey, filex_start, filex_end);
				}

				filey = glyph->y;
				filex_start = glyph->x;
				filex_end = glyph->x + glyph->x_advance;
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
		g.index = glyph->glyph_index;
		g.x = glyph->x;
		g.y = glyph->y;

		growable_glyph_array_append(gga_current, g);

		if (glyph->code == '\05') {
			cairo_arc(cr, g.x + glyph->x_advance/2, g.y - editor->buffer->line_height/2, 1, 0, 2*M_PI);
			cairo_fill(cr);
		}
	}

	if (onfile && (gga_current != NULL)) {
		growable_glyph_array_append_underline(gga_current, filey, filex_start, filex_end);
	}

	if (editor->first_exposed < 0) editor->first_exposed = 0;
}

static void draw_cursorline(cairo_t *cr, editor_t *editor) {
	if (!(editor->cursor_visible)) return;
	if (editor->buffer->cursor < 0) return;
	if (editor->buffer->single_line) return;

	GtkAllocation allocation;
	gtk_widget_get_allocation(editor->drar, &allocation);

	double cursor_x, cursor_y;
	line_get_glyph_coordinates(editor->buffer, editor->buffer->cursor, &cursor_x, &cursor_y);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_BG_CURSORLINE));
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle(cr, cursor_x - allocation.width, cursor_y-editor->buffer->ascent, 2*allocation.width, editor->buffer->ascent+editor->buffer->descent);
	cairo_fill(cr);
}

#define CURSOR_WIDTH 1

static void draw_cursor(cairo_t *cr, editor_t *editor) {
	if (!(editor->cursor_visible)) return;
	if (editor->research.mode != SM_NONE) return;

	double cursor_x, cursor_y;
	line_get_glyph_coordinates(editor->buffer, editor->buffer->cursor, &cursor_x, &cursor_y);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_FG_COLOR));
	cairo_rectangle(cr, cursor_x, cursor_y-editor->buffer->ascent, CURSOR_WIDTH, editor->buffer->ascent+editor->buffer->descent);
	cairo_fill(cr);
}

static void draw_underline(editor_t *editor, cairo_t *cr, struct growable_glyph_array *gga) {
	for (int i = 0; i < gga->underline_n; ++i) {
		struct underline_info_t *u = (gga->underline_info + i);
		//cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_rectangle(cr, u->filex_start, u->filey - editor->buffer->underline_position, u->filex_end - u->filex_start, editor->buffer->underline_thickness);
		cairo_fill(cr);
	}
}

static void draw_posbox(cairo_t *cr, editor_t *editor, GtkAllocation *allocation) {
	if (editor->buffer->single_line) return;

	char *posbox_text;
	cairo_text_extents_t posbox_ext;
	double x, y;

	asprintf(&posbox_text, "=%d %d:%d %0.0f%%", editor->buffer->cursor, editor->lineno, editor->colno, (100.0 * editor->buffer->cursor / BSIZE(editor->buffer)));

	cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config_strval(&(editor->buffer->config), CFG_POSBOX_FONT), 0));

	cairo_text_extents(cr, posbox_text, &posbox_ext);

	y = allocation->height - posbox_ext.height - 4.0;
	x = allocation->width - posbox_ext.x_advance - 4.0;

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_POSBOX_BORDER_COLOR));
	cairo_rectangle(cr, x-1.0, y-2.0, posbox_ext.x_advance+4.0, posbox_ext.height+4.0);
	cairo_fill(cr);
	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_POSBOX_BG_COLOR));
	cairo_rectangle(cr, x, y-1.0, posbox_ext.x_advance + 2.0, posbox_ext.height + 2.0);
	cairo_fill(cr);

	cairo_move_to(cr, x+1.0, y+posbox_ext.height);
	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_POSBOX_FG_COLOR));
	cairo_show_text(cr, posbox_text);

	free(posbox_text);
}

static void draw_notices(cairo_t *cr, editor_t *editor, GtkAllocation *allocation) {
	if (editor->buffer->single_line) return;

	cairo_set_scaled_font(cr, fontset_get_cairofont_by_name(config_strval(&(editor->buffer->config), CFG_NOTICE_FONT), 0));

	if (editor->buffer->stale) {
		roundbox(cr, allocation, "Content of this file changed on disk, reload [y/n]?");
		return;
	}

	switch (editor->research.mode) {
	case SM_REGEXP: {
		char *msg;
		if (editor->research.cmd != NULL) {
			asprintf(&msg, "Searching /%s/ executing {%s}... ([a] apply, [A] apply to all, [n] next, [q] quit)", editor->research.regexpstr, editor->research.cmd);
		} else {
			asprintf(&msg, "Searching /%s/... ([n] next, [q] quit)", editor->research.regexpstr);
		}
		alloc_assert(msg);
		roundbox(cr, allocation, msg);
		free(msg);
		break;
	}

	case SM_LITERAL:
		roundbox(cr, allocation, "Searching...");
		break;

	default:
		// Nothing
		break;
	}
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, editor_t *editor) {
	cairo_t *cr = gdk_cairo_create(widget->window);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_BG_COLOR));
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	buffer_typeset_maybe(editor->buffer, allocation.width, false);
	int sel_invert = config_intval(&(editor->buffer->config), CFG_EDITOR_SEL_INVERT);

	/********** TRANSLATED STUFF STARTS HERE  ***************************/
	cairo_translate(cr, -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

	draw_cursorline(cr, editor);
	if (!sel_invert) draw_selection(editor, allocation.width, cr, sel_invert);

	//editor->buffer->rendered_height = 0.0;

	GHashTable *ht = g_hash_table_new(g_direct_hash, g_direct_equal);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	set_color_cfg(cr, config_intval(&(editor->buffer->config), CFG_EDITOR_FG_COLOR));

	draw_lines(editor, &allocation, cr, ht, gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) + allocation.height);

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

	g_hash_table_destroy(ht);

	if (sel_invert) draw_selection(editor, allocation.width, cr, sel_invert);
	draw_parmatch(editor, &allocation, cr);

	draw_cursor(cr, editor);

	/********** NOTHING IS TRANSLATED BEYOND THIS ***************************/
	cairo_translate(cr, gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

	draw_posbox(cr, editor, &allocation);
	draw_notices(cr, editor, &allocation);

	if (editor->buffer->single_line) {
		gtk_widget_hide(GTK_WIDGET(editor->drarscroll));
	} else {
		gtk_adjustment_set_upper(GTK_ADJUSTMENT(editor->adjustment), editor->buffer->rendered_height + (allocation.height / 2));
		gtk_adjustment_set_page_size(GTK_ADJUSTMENT(editor->adjustment), allocation.height);
		gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(editor->adjustment), allocation.height/2);
		gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(editor->adjustment), editor->buffer->line_height);

		if ((editor->buffer->rendered_width > allocation.width) && (config_intval(&(editor->buffer->config), CFG_AUTOWRAP) == 0)) {
			gtk_adjustment_set_upper(GTK_ADJUSTMENT(editor->hadjustment), editor->buffer->left_margin + editor->buffer->rendered_width + editor->buffer->right_margin);
			gtk_adjustment_set_page_size(GTK_ADJUSTMENT(editor->hadjustment), allocation.width);
			gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(editor->hadjustment), allocation.width/2);
			gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(editor->hadjustment), editor->buffer->em_advance);
		}
	}

	cairo_destroy(cr);

	if (editor->center_on_cursor_after_next_expose) {
		editor->center_on_cursor_after_next_expose = FALSE;
		editor_include_cursor(editor, ICM_MID, ICM_MID);
	}

	if (editor->warp_mouse_after_next_expose) {
		editor_grab_focus(editor, true);
		editor->warp_mouse_after_next_expose = FALSE;
	}

	return TRUE;
}

static gboolean scrolled_callback(GtkAdjustment *adj, editor_t *editor) {
	lexy_update_resume(editor->buffer);
	gtk_widget_queue_draw(editor->drar);
	return TRUE;
}

static gboolean editor_focusin_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	editor->cursor_visible = 1;
	gtk_widget_queue_draw(editor->drar);
	return FALSE;
}

static gboolean editor_focusout_callback(GtkWidget *widget, GdkEventFocus *event, editor_t *editor) {
	compl_wnd_hide(editor->completer);
	compl_wnd_hide(editor->completer);
	editor->cursor_visible = 0;
	gtk_widget_queue_draw(editor->drar);
	end_selection_scroll(editor);
	return FALSE;
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
	if (selection == NULL) return;

	open_link(editor, islink, selection);

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
	r->completer = &the_word_completer;
	r->alt_completer = NULL;
	r->dirty_line = false;
	r->first_exposed = 0;

	if (buffer != NULL) {
		r->lineno = buffer_line_of(buffer, buffer->cursor);
		r->colno = buffer_column_of(buffer, buffer->cursor);
	} else {
		r->lineno = 1; r->colno = 1;
	}

	r->single_line_escape = NULL;
	r->single_line_return = NULL;

	r->drar = gtk_drawing_area_new();
	r->drarim = gtk_im_multicontext_new();

	r->selection_scroll_timer = -1;

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
	r->hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0);

	gtk_table_attach(GTK_TABLE(r), r->drar, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(r), r->drarscroll, 1, 2, 0, 1, 0, GTK_EXPAND|GTK_FILL, 0, 0);

	g_signal_connect(G_OBJECT(r->drarscroll), "value_changed", G_CALLBACK(scrolled_callback), (gpointer)r);

	if (single_line) {
		gtk_widget_set_size_request(r->drar, 1, r->buffer->line_height + config_intval(&(buffer->config), CFG_MAIN_FONT_HEIGHT_REDUCTION));
	}

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
	gtk_widget_grab_focus(editor->drar);
	editor->cursor_visible = TRUE;

	column_t *col;
	tframe_t *frame;
	find_editor_for_buffer(editor->buffer, &col, &frame, NULL);
	if ((col != NULL) && (frame != NULL)) column_expand_frame(col, frame);

	int warptype = config_intval(&(editor->buffer->config), CFG_WARP_MOUSE);

	if (warptype && warp) {
		GdkDisplay *display = gdk_display_get_default();
		GdkScreen *screen = gdk_display_get_default_screen(display);
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		if ((allocation.x < 0) || (allocation.y < 0)) {
			editor->warp_mouse_after_next_expose = TRUE;
		} else {
			gint x = allocation.x, y = allocation.y;
			gint wpos_x, wpos_y;
			gdk_window_get_position(gtk_widget_get_window(GTK_WIDGET(columnset)), &wpos_x, &wpos_y);
			x += wpos_x; y += wpos_y;

			if (warptype > 1) {
				double cx, cy;
				line_get_glyph_coordinates(editor->buffer, editor->buffer->cursor, &cx, &cy);
				y += cy - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
				x += 5;
			} else {
				x += 5; y += 5;
			}

			gdk_display_warp_pointer(display, screen, x, y);
		}
	}
}

void editor_start_search(editor_t *editor, enum search_mode_t search_mode, const char *initial_search_term) {
	if (editor->buffer->single_line) return;

	editor->buffer->mark = editor->buffer->cursor;

	editor->research.search_failed = false;
	editor->research.mode = search_mode;

	if (editor->research.mode == SM_LITERAL) {
		if (initial_search_term != NULL) {
			editor->research.literal_text = utf8_to_utf32_string(initial_search_term, &(editor->research.literal_text_cap));
			editor->research.literal_text_allocated = strlen(initial_search_term);
		} else {
			editor->research.literal_text = malloc(sizeof(uint32_t) * 5);
			editor->research.literal_text_allocated = 5;
			editor->research.literal_text_cap = 0;
		}
	}

	move_search(editor, true, true, false);
	editor_grab_focus(editor, false);
	gtk_widget_queue_draw(GTK_WIDGET(editor));
}