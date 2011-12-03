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
#include "reshandle.h"
#include "baux.h"
#include "go.h"
#include "editor_cmdline.h"
#include "cfg.h"

void set_label_text(editor_t *editor) {
	char *labeltxt;
	
	if ((strcmp(editor->label_state, "cmd") == 0) && (editor->locked_command_line[0] != '\0')) {
		asprintf(&labeltxt, " %s | cmd<%s>", editor->buffer->name, editor->locked_command_line);
	} else {
		asprintf(&labeltxt, " %s | %s>", editor->buffer->name, editor->label_state);
	}

	editor->reshandle->modified = editor->buffer->modified;

	gtk_label_set_text(GTK_LABEL(editor->label), labeltxt);
	gtk_widget_queue_draw(editor->label);
	gtk_widget_queue_draw(editor->reshandle->resdr);

	free(labeltxt);
}

void editor_complete_edit(editor_t *editor) {
	active_column = editor->column;
	set_label_text(editor);
	editor_center_on_cursor(editor);
	gtk_widget_queue_draw(editor->drar);
}

void editor_replace_selection(editor_t *editor, const char *new_text) {
	buffer_replace_selection(editor->buffer, new_text);
	editor_complete_edit(editor);
}

void editor_center_on_cursor(editor_t *editor) {
	double x, y;
	double translated_y;
	GtkAllocation allocation;

	gtk_widget_get_allocation(editor->drar, &allocation);
	buffer_cursor_position(editor->buffer, &x, &y);

	translated_y = y - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	if ((translated_y < 0) || (translated_y > allocation.height)) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), y - allocation.height / 2);
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
}

static void copy_selection_to_clipboard(editor_t *editor, GtkClipboard *clipboard) {
	lpoint_t start, end;
	char *r = NULL;
	
	buffer_get_selection(editor->buffer, &start, &end);
 
	if (start.line == NULL) return;
	if (end.line == NULL) return;

	r  = buffer_lines_to_text(editor->buffer, &start, &end);
	
	if (strcmp(r, "") != 0)
		gtk_clipboard_set_text(clipboard, r, -1);

	free(r);
}

void editor_complete_move(editor_t *editor, gboolean should_move_origin) {
	gtk_widget_queue_draw(editor->drar);

	editor->cursor_visible = TRUE;

	if (should_move_origin) {
		editor_center_on_cursor(editor);
	} else {
		gtk_widget_queue_draw(editor->drar);
	}

	copy_selection_to_clipboard(editor, selection_clipboard);
}

void editor_move_cursor(editor_t *editor, int delta_line, int delta_char, enum MoveCursorSpecial special, gboolean should_move_origin) {

#ifdef COMPLEX_LINE_MOVE
	int i = 0;

	if (delta_line > 0) {
		for (i = 0; i < delta_line; ++i) {
			if (editor->buffer->cursor.glyph < editor->buffer->cursor.line->cap) {
				int j = 0;
				int found = 0;
				double cury = editor->buffer->cursor.line->glyphs[editor->buffer->cursor.glyph].y;
				double wantedx = editor->buffer->cursor.line->glyphs[editor->buffer->cursor.glyph].x;
				for (j = editor->buffer->cursor.glyph; j < editor->buffer->cursor.line->cap; ++j) {
					if (fabs(cury - editor->buffer->cursor.line->glyphs[j].y) > 0.001) {
						//printf("Actually found %g %g\n", cury, buffer->cursor.line->glyphs[j].y);
						editor->buffer->cursor.glyph = j;
						cury = editor->buffer->cursor.line->glyphs[j].y;
						found = 1;
						break;
					}
				}
				if (found) {
					for ( ; editor->buffer->cursor.glyph < editor->buffer->cursor.line->cap; ++(editor->buffer->cursor.glyph)) {
						double diff = wantedx - editor->buffer->cursor.line->glyphs[editor->buffer->cursor.glyph].x;
						if (fabs(cury - editor->buffer->cursor.line->glyphs[editor->buffer->cursor.glyph].y) > 0.001) {
							--(editor->buffer->cursor.glyph);
							break;
						}

						if (diff <= 0) {
							if (fabs(diff) > editor->buffer->em_advance/2) {
								--(editor->buffer->cursor.glyph);
							}
							break;
						}
					}
					continue;
				}
			}

			if (editor->buffer->cursor.line->next == NULL) break;
			editor->buffer->cursor.line = editor->buffer->cursor.line->next;
			if (editor->buffer->cursor.glyph > editor->buffer->cursor.line->cap)
				editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
		}
	}

	if (delta_line < 0) {
		for (i = 0; i < abs(delta_line); ++i) {
			int j = 0;
			int found = 0;
			double cury, wantedx;
			line_get_glyph_coordinates(editor->buffer, &(editor->buffer->cursor), &wantedx, &cury);
			for (j = editor->buffer->cursor.glyph-1; j >= 0; --j) {
				if (fabs(cury - editor->buffer->cursor.line->glyphs[j].y) > 0.001) {
					editor->buffer->cursor.glyph = j;
					cury = editor->buffer->cursor.line->glyphs[j].y;
					found = 1;
					break;
				}
			}

			if (found) {
				for ( ; editor->buffer->cursor.glyph > 0; --(editor->buffer->cursor.glyph)) {
					double diff  = wantedx - editor->buffer->cursor.line->glyphs[editor->buffer->cursor.glyph].x;
					if (fabs(cury - editor->buffer->cursor.line->glyphs[editor->buffer->cursor.glyph].y) > 0.001) {
						++(editor->buffer->cursor.glyph);
						break;
					}
					if (diff >= 0) {
						if (fabs(diff) < editor->buffer->em_advance/2) {
							++(editor->buffer->cursor.glyph);
						}
						break;
					}
				}
				continue;
			}
			

			if (editor->buffer->cursor.line->prev == NULL) break;
			editor->buffer->cursor.line = editor->buffer->cursor.line->prev;
			if (editor->buffer->cursor.glyph > editor->buffer->cursor.line->cap)
				editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
		}
	}
#else
	while (delta_line < 0) {
		real_line_t *to = editor->buffer->cursor.line->prev;
		if (to == NULL) break;
		editor->buffer->cursor.line = to;
		++delta_line;
	}
	
	while (delta_line > 0) {
		real_line_t *to = editor->buffer->cursor.line->next;
		if (to == NULL) break;
		editor->buffer->cursor.line = to;
		--delta_line;
	}
	
	if (editor->buffer->cursor.glyph > editor->buffer->cursor.line->cap)
		editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
#endif

	if ((delta_char != 0) || (special != MOVE_NORMAL)) {
		editor->buffer->cursor.glyph += delta_char;

		if (editor->buffer->cursor.glyph < 0) editor->buffer->cursor.glyph = 0;
		if (editor->buffer->cursor.glyph > editor->buffer->cursor.line->cap) editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;

		if (special == MOVE_LINE_START) {
			editor->buffer->cursor.glyph = 0;
		} else if (special == MOVE_LINE_END) {
			editor->buffer->cursor.glyph = editor->buffer->cursor.line->cap;
		}
	}
	
	buffer_extend_selection_by_select_type(editor->buffer);

	editor_complete_move(editor, should_move_origin);
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

void quick_message(editor_t *editor, const char *title, const char *msg) {
	GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(editor->window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *label = gtk_label_new(msg);

	g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
	
	gtk_container_add(GTK_CONTAINER(content_area), label);
	gtk_widget_show_all(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
}

void editor_close_editor(editor_t *editor) {
	if (column_editor_count(editor->column) > 1) {
		editor = column_remove(editor->column, editor);
	} else {
		column_t *column = columns_remove(editor->column, editor);
		editor = column_get_first_editor(column);
	}
	editor_grab_focus(editor);
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

void editor_mark_action(editor_t *editor) {
	if (editor->buffer->mark.line == NULL) {
		buffer_set_mark_at_cursor(editor->buffer);
	} else {
		buffer_unset_mark(editor->buffer);
	}
	gtk_widget_queue_draw(editor->drar);
}

void editor_copy_action(editor_t *editor) {
	if (editor->buffer->mark.line != NULL) {
		copy_selection_to_clipboard(editor, default_clipboard);
		buffer_unset_mark(editor->buffer);
		gtk_widget_queue_draw(editor->drar);
	}
}

void editor_cut_action(editor_t *editor) {
	if (editor->buffer->mark.line != NULL) {
		copy_selection_to_clipboard(editor, default_clipboard);
		editor_replace_selection(editor, "");
		gtk_widget_queue_draw(editor->drar);
	}
}

void editor_save_action(editor_t *editor) {
	save_to_text_file(editor->buffer);
	set_label_text(editor);
}

void editor_undo_action(editor_t *editor) {
	buffer_undo(editor->buffer);
	active_column = editor->column;
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, editor_t *editor) {
	char pressed[40];
	const char *converted;
	const char *command;
	GtkAllocation allocation;
	int shift = event->state & GDK_SHIFT_MASK;
	int ctrl = event->state & GDK_CONTROL_MASK;
	int alt = event->state & GDK_MOD1_MASK;
	int super = event->state & GDK_SUPER_MASK;

	gtk_widget_get_allocation(editor->drar, &allocation);

	/* Default key bindings */
	if (!shift && !ctrl && !alt && !super) {
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
			/*{
				double nv = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) + gtk_adjustment_get_page_increment(GTK_ADJUSTMENT(editor->adjustment));
				double mv = gtk_adjustment_get_upper(GTK_ADJUSTMENT(editor->adjustment)) - gtk_adjustment_get_page_size(GTK_ADJUSTMENT(editor->adjustment));
				if (nv > mv) nv = mv;
				gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), nv);
				

				}*/
			return TRUE;
			
		case GDK_KEY_Home:
			buffer_aux_go_first_nonws_or_0(editor->buffer);
			editor_complete_move(editor, TRUE);
			return TRUE;
		case GDK_KEY_End:
			editor_move_cursor(editor, 0, 0, MOVE_LINE_END, TRUE);
			return TRUE;

		case GDK_KEY_Tab:
			editor_replace_selection(editor, "\t");
			return TRUE;

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
		case GDK_KEY_Return: {
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
		case GDK_KEY_Escape:
			return TRUE;
		default: // At least one modifier must be pressed to activate special actions
			goto im_context;
		}
	}

	if (shift && !ctrl && !alt && !super) {
		if ((event->keyval >= 0x21) && (event->keyval <= 0x7e)) {
			goto im_context;
		}
	}
	
	converted = keyevent_to_string(event->keyval);

	if (converted == NULL) goto im_context;
	
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
		if ((event->keyval < 0x21) || (event->keyval > 0x7e)) {
			strcat(pressed, "Shift-");
		}
	}

	strcat(pressed, converted);

	command = g_hash_table_lookup(keybindings, pressed);
	//printf("Keybinding [%s] -> {%s}\n", pressed, command);

	if (command != NULL) {
		interp_eval(editor, command);
	}

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
			gtk_widget_grab_focus(editor->entry);
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
		} else if (event->type == GDK_3BUTTON_PRESS) {
			buffer_change_select_type(editor->buffer, BST_LINES);
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
			mouse_open_action(editor, &start, NULL);
		} else if (inbetween_lpoint(&start, &(editor->buffer->cursor), &end)) {
			mouse_open_action(editor, &start, &end);
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
	copy_selection_to_clipboard(editor, selection_clipboard);
	//editor_center_on_cursor(editor);
	editor_include_cursor(editor);
	gtk_widget_queue_draw(editor->drar);
}

static bool y_at_border(editor_t *editor, double y, GtkAllocation *allocation) {
	//return (y > (allocation->height - editor->buffer->line_height)) || (y < editor->buffer->line_height);
	return (y > allocation->height) || (y < 0);
}

static gboolean motion_callback(GtkWidget *widget, GdkEventMotion *event, editor_t *editor) {
	if (editor->mouse_marking) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		if (!y_at_border(editor, event->y, &allocation)) {
			selection_move(editor, event->x, event->y);
		}
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

static gboolean cursor_blinker(editor_t *editor) {
	if (!(editor->initialization_ended)) return TRUE;
	if (editor->cursor_visible < 0) editor->cursor_visible = 1;

	if (!gtk_widget_is_focus(editor->drar)) {
		if (editor->cursor_visible) {
			editor->cursor_visible = 0;
			gtk_widget_queue_draw(editor->drar);
		}
	} else {
		editor->cursor_visible = (editor->cursor_visible + 1) % 3;
		if (editor->mouse_marking) {
			GtkAllocation allocation;
			int x, y;
			gtk_widget_get_allocation(editor->drar, &allocation);
			gtk_widget_get_pointer(editor->drar, &x, &y);
			if (y_at_border(editor, y, &allocation)) {
				selection_move(editor, x, y+editor->buffer->line_height);
			}
		}
		gtk_widget_queue_draw(editor->drar);
	}
	
	return TRUE;
}

static void draw_line(editor_t *editor, GtkAllocation *allocation, cairo_t *cr, real_line_t *line) {
	cairo_show_glyphs(cr, line->glyphs, line->cap);

	double cury = line->start_y;

	for (int i = 0; i < line->cap; ++i) {
		// draws soft wrapping indicators
		if (line->glyphs[i].y - cury > 0.001) {
			/* draw ending tract */
			cairo_set_line_width(cr, 4.0);
			cairo_move_to(cr, line->glyphs[i-1].x + line->glyph_info[i-1].x_advance, cury-(editor->buffer->ex_height/2.0));
			cairo_line_to(cr, allocation->width, cury-(editor->buffer->ex_height/2.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 2.0);

			cury = line->glyphs[i].y;

			/* draw initial tract */
			cairo_set_line_width(cr, 4.0);
			cairo_move_to(cr, 0.0, cury-(editor->buffer->ex_height/2.0));
			cairo_line_to(cr, editor->buffer->left_margin, cury-(editor->buffer->ex_height/2.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 2.0);
		}
	}
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, editor_t *editor) {
	cairo_t *cr = gdk_cairo_create(widget->window);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	if (!(editor->initialization_ended)) {
		gdk_window_set_cursor(gtk_widget_get_window(editor->label), gdk_cursor_new(GDK_FLEUR));
		editor->timeout_id = g_timeout_add(500, (GSourceFunc)cursor_blinker, (gpointer)editor);
		editor->initialization_ended = 1;
	}

	if (selection_target_buffer != NULL) {
		gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_ICON));
	} else {
		gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_XTERM));
	}

	set_color_cfg(cr, config[CFG_EDITOR_BG_COLOR].intval);
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	set_color_cfg(cr, config[CFG_EDITOR_FG_COLOR].intval);
	cairo_set_scaled_font(cr, editor->buffer->main_font.cairofont);

	cairo_translate(cr, -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

	buffer_typeset_maybe(editor->buffer, allocation.width);

	editor->buffer->rendered_height = 0.0;

	double originy = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

	int count = 0;
	for (real_line_t *line = editor->buffer->real_line; line != NULL; line = line->next) {
		if (((line->start_y + line->y_increment - originy) > 0) && ((line->start_y - editor->buffer->ascent - originy) < allocation.height)) {
			draw_line(editor, &allocation, cr, line);
		}
		
		++count;

		editor->buffer->rendered_height += line->y_increment;
	}

	draw_selection(editor, allocation.width, cr);

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
		
		cairo_set_scaled_font(cr, editor->buffer->posbox_font.cairofont);

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
		gtk_adjustment_set_upper(GTK_ADJUSTMENT(editor->hadjustment), editor->buffer->left_margin + editor->buffer->rendered_width);
		gtk_adjustment_set_page_size(GTK_ADJUSTMENT(editor->hadjustment), allocation.width);
		gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(editor->hadjustment), allocation.width/2);
		gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(editor->hadjustment), editor->buffer->em_advance);
	}

	cairo_destroy(cr);

	editor->initialization_ended = 1;
	
	if (editor->center_on_cursor_after_next_expose) {
		editor->center_on_cursor_after_next_expose = FALSE;
		editor_center_on_cursor(editor);
	}
	
	if (editor->warp_mouse_after_next_expose) {
		editor_grab_focus(editor);
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

static gboolean label_button_press_callback(GtkWidget *widget, GdkEventButton *event, editor_t *editor) {
	if ((event->type == GDK_2BUTTON_PRESS) && (event->button == 1)) {
		if (column_remove_others(editor->column, editor) == 0) {
			columns_remove_others(editor->column, editor);
		}
		return TRUE;
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 2)) {
		editor_close_editor(editor);
		return TRUE;
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
		editor_t *new_editor = column_new_editor(editor->column, null_buffer());
		if (new_editor == NULL) {
			heuristic_new_frame(editor, null_buffer());
		} else {
           editor_grab_focus(new_editor);
		}
		return TRUE;
	}

	return FALSE;
}

static gboolean label_button_release_callback(GtkWidget *widget, GdkEventButton *event, editor_t *editor) {
	GtkAllocation allocation;
	double x, y;

	gtk_widget_get_allocation(widget, &allocation);

	x = event->x + allocation.x;
	y = event->y + allocation.y;

	if (event->button == 1) {
		editor_t *target = columns_get_editor_from_positioon(x, y);
		
		if ((target != NULL) && (target != editor)) {
			buffer_t *tbuf = target->buffer;
			editor_switch_buffer(target, editor->buffer);
			editor_switch_buffer(editor, tbuf);
		}
		return TRUE;
	} else if (event->button == 3) {
		column_t *target_column = columns_get_column_from_position(x, y);
		if ((target_column != NULL) && (target_column != editor->column)) {
			columns_swap_columns(editor->column, target_column);
		}
		return TRUE;
	}
	
	return FALSE;
}


editor_t *new_editor(GtkWidget *window, column_t *column, buffer_t *buffer) {
	editor_t *r = malloc(sizeof(editor_t));

	r->column = column;
	r->window = window;
	r->buffer = buffer;
	r->cursor_visible = TRUE;
	r->initialization_ended = 0;
	r->mouse_marking = 0;
	r->ignore_next_entry_keyrelease = FALSE;
	r->center_on_cursor_after_next_expose = FALSE;
	r->warp_mouse_after_next_expose = FALSE;

	r->search_mode = FALSE;
	r->search_failed = FALSE;

	r->drar = gtk_drawing_area_new();
	r->drarim = gtk_im_multicontext_new();

	r->timeout_id = -1;
	
	strcpy(r->locked_command_line, "");

	gtk_widget_add_events(r->drar, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

	gtk_widget_set_can_focus(GTK_WIDGET(r->drar), TRUE);

	g_signal_connect(G_OBJECT(r->drar), "expose_event",
                     G_CALLBACK(expose_event_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "key-press-event",
                     G_CALLBACK(key_press_callback), r);
	g_signal_connect(G_OBJECT(r->drar), "key-release-event",
                     G_CALLBACK(key_release_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "button-press-event",
                     G_CALLBACK(button_press_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "button-release-event",
                     G_CALLBACK(button_release_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "scroll-event",
                     G_CALLBACK(scroll_callback), r);

	g_signal_connect(G_OBJECT(r->drar), "motion-notify-event",
                     G_CALLBACK(motion_callback), r);

	g_signal_connect(G_OBJECT(r->drarim), "commit",
                     G_CALLBACK(text_entry_callback), r);

	{
		GtkWidget *drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(r->adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
		r->drarhscroll = gtk_hscrollbar_new((GtkAdjustment *)(r->hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
		GtkWidget *tag = gtk_hbox_new(FALSE, 0);
		GtkBorder bor = { 0, 0, 0, 0 };
		GtkWidget *event_box = gtk_event_box_new();
		GtkWidget *table = gtk_table_new(0, 0, FALSE);
		
		/*r->container = gtk_frame_new(NULL);
		
		gtk_container_add(GTK_CONTAINER(r->container), table);*/
		
		r->container = table;

		r->reshandle = reshandle_new(column, r);
		r->label = gtk_label_new("");
		r->entry = gtk_entry_new();

		gtk_container_add(GTK_CONTAINER(event_box), r->label);
		
		gtk_widget_set_size_request(r->entry, 10, 16);
		gtk_entry_set_inner_border(GTK_ENTRY(r->entry), &bor);
		gtk_entry_set_has_frame(GTK_ENTRY(r->entry), FALSE);
		gtk_widget_modify_font(r->entry, elements_font_description);
		gtk_widget_modify_font(r->label, elements_font_description);

		entry_callback_setup(r);
		
		r->label_state = "cmd";
		set_label_text(r);

		gtk_widget_add_events(event_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
		
		g_signal_connect(event_box, "button-press-event", G_CALLBACK(label_button_press_callback), r);
		g_signal_connect(event_box, "button-release-event", G_CALLBACK(label_button_release_callback), r);
		
		gtk_container_add(GTK_CONTAINER(tag), r->reshandle->resdr);
		gtk_container_add(GTK_CONTAINER(tag), event_box);
		gtk_container_add(GTK_CONTAINER(tag), r->entry);

		gtk_box_set_child_packing(GTK_BOX(tag), r->reshandle->resdr, FALSE, FALSE, 0, GTK_PACK_START);
		gtk_box_set_child_packing(GTK_BOX(tag), event_box, FALSE, FALSE, 0, GTK_PACK_START);
		gtk_box_set_child_packing(GTK_BOX(tag), r->entry, TRUE, TRUE, 0, GTK_PACK_END);

		place_frame_piece(table, TRUE, 0, 2); // top frame
		place_frame_piece(table, FALSE, 2, 5); // right frame
		
		gtk_table_attach(GTK_TABLE(table), tag, 0, 2, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

		place_frame_piece(table, TRUE, 2, 2); // command line frame
		
		gtk_table_attach(GTK_TABLE(table), r->drar, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
		gtk_table_attach(GTK_TABLE(table), drarscroll, 1, 2, 3, 4, 0, GTK_EXPAND|GTK_FILL, 0, 0);
		gtk_table_attach(GTK_TABLE(table), r->drarhscroll, 0, 1, 4, 5, GTK_EXPAND|GTK_FILL, 0, 0, 0);

		g_signal_connect(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), (gpointer)r);
		g_signal_connect(G_OBJECT(r->drarhscroll), "value_changed", G_CALLBACK(hscrolled_callback), (gpointer)r);
	}

	return r;
}

gint editor_get_height_request(editor_t *editor) {
	int lines = buffer_real_line_count(editor->buffer);
	if (lines > MAX_LINES_HEIGHT_REQUEST) lines = MAX_LINES_HEIGHT_REQUEST;
	if (lines < MIN_LINES_HEIGHT_REQUEST) lines = MIN_LINES_HEIGHT_REQUEST;
	return (lines+2) * editor->buffer->line_height;
}

void editor_free(editor_t *editor) {
	editor->initialization_ended = 0;
	if (editor->timeout_id != -1) {
		g_source_remove(editor->timeout_id);
	}
	reshandle_free(editor->reshandle);
	//gtk_widget_destroy(editor->table);
	free(editor);
}

void editor_grab_focus(editor_t *editor) {
	gtk_widget_grab_focus(editor->drar);
	
	if (config[CFG_WARP_MOUSE].intval) {
		GdkDisplay *display = gdk_display_get_default();
		GdkScreen *screen = gdk_display_get_default_screen(display);
		GtkAllocation allocation;
		gtk_widget_get_allocation(editor->drar, &allocation);
		if ((allocation.x < 0) || (allocation.y < 0)) {
			editor->warp_mouse_after_next_expose = TRUE;
		} else {
			gint wpos_x, wpos_y;
			gdk_window_get_position(gtk_widget_get_window(editor->window), &wpos_x, &wpos_y);
			
			//printf("allocation: %d,%d\n", allocation.x, allocation.y);
			gdk_display_warp_pointer(display, screen, allocation.x+wpos_x+5, allocation.y+wpos_y+5);
		}
	}
}
