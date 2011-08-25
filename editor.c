/* Questa e` una lunga linea inserita qui per testare il funzionamento dell'editor sulle linee lunghe, questa e` una lunga linea inserita qui per testare il funzionamento dell'editor sulle linee lunghe */
#include "editor.h"

#include <math.h>
#include <assert.h>

#include <tcl.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"
#include "buffers.h"
#include "interp.h"
#include "editors.h"

static void set_label_text(editor_t *editor) {
    char *labeltxt;

    asprintf(&labeltxt, "%s %s  |  %s>", (editor->buffer->modified ? "**" : "  "), editor->buffer->name, editor->label_state);

    gtk_label_set_text(GTK_LABEL(editor->label), labeltxt);
    gtk_widget_queue_draw(editor->label);

    free(labeltxt); 
}

static void editor_replace_selection(editor_t *editor, const char *new_text) {
    buffer_replace_selection(editor->buffer, new_text);
    editor->buffer->modified = 1;
    set_label_text(editor);
    editors_queue_draw_for_buffer(editor->buffer);
}

static void editor_center_on_cursor(editor_t *editor) {
    double x, y;
    double translated_y;
    GtkAllocation allocation;

    gtk_widget_get_allocation(editor->drar, &allocation);
    buffer_cursor_position(editor->buffer, &x, &y);

    translated_y = y - gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

    if ((translated_y < 0) || (translated_y > allocation.height)) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), y - allocation.height / 2);
    }

    return;
}

static void copy_selection_to_clipboard(editor_t *editor, GtkClipboard *clipboard) {
    real_line_t *start_line, *end_line;
    int start_glyph, end_glyph;
    char *r = NULL;
    
    buffer_get_selection(editor->buffer, &start_line, &start_glyph, &end_line, &end_glyph);
 
    if (start_line == NULL) return;
    if (end_line == NULL) return;

    r  = buffer_lines_to_text(editor->buffer, start_line, end_line, start_glyph, end_glyph);    

    gtk_clipboard_set_text(clipboard, r, -1);

    free(r);
}

enum MoveCursorSpecial {
    MOVE_NORMAL = 1,
    MOVE_LINE_START,
    MOVE_LINE_END,
};

static void move_cursor(editor_t *editor, int delta_line, int delta_char, enum MoveCursorSpecial special, gboolean should_move_origin) {
    int i = 0;

    gtk_widget_queue_draw(editor->drar);

    if (delta_line > 0) {
        for (i = 0; i < delta_line; ++i) {
            if (editor->buffer->cursor_glyph < editor->buffer->cursor_line->cap) {
                int j = 0;
                int found = 0;
                double cury = editor->buffer->cursor_line->glyphs[editor->buffer->cursor_glyph].y;
                double wantedx = editor->buffer->cursor_line->glyphs[editor->buffer->cursor_glyph].x;
                for (j = editor->buffer->cursor_glyph; j < editor->buffer->cursor_line->cap; ++j) {
                    if (fabs(cury - editor->buffer->cursor_line->glyphs[j].y) > 0.001) {
                        //printf("Actually found %g %g\n", cury, buffer->cursor_line->glyphs[j].y);
                        editor->buffer->cursor_glyph = j;
                        cury = editor->buffer->cursor_line->glyphs[j].y;
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    for ( ; editor->buffer->cursor_glyph < editor->buffer->cursor_line->cap; ++(editor->buffer->cursor_glyph)) {
                        double diff = wantedx - editor->buffer->cursor_line->glyphs[editor->buffer->cursor_glyph].x;
                        if (fabs(cury - editor->buffer->cursor_line->glyphs[editor->buffer->cursor_glyph].y) > 0.001) {
                            --(editor->buffer->cursor_glyph);
                            break;
                        }

                        if (diff <= 0) {
                            if (fabs(diff) > editor->buffer->em_advance/2) {
                                --(editor->buffer->cursor_glyph);
                            }
                            break;
                        }
                    }
                    continue;
                }
            }

            if (editor->buffer->cursor_line->next == NULL) break;
            editor->buffer->cursor_line = editor->buffer->cursor_line->next;
            if (editor->buffer->cursor_glyph > editor->buffer->cursor_line->cap)
                editor->buffer->cursor_glyph = editor->buffer->cursor_line->cap;
        }
    }

    if (delta_line < 0) {
        for (i = 0; i < abs(delta_line); ++i) {
            int j = 0;
            int found = 0;
            double cury, wantedx;
            line_get_glyph_coordinates(editor->buffer, editor->buffer->cursor_line, editor->buffer->cursor_glyph, &wantedx, &cury);
            for (j = editor->buffer->cursor_glyph-1; j >= 0; --j) {
                if (fabs(cury - editor->buffer->cursor_line->glyphs[j].y) > 0.001) {
                    editor->buffer->cursor_glyph = j;
                    cury = editor->buffer->cursor_line->glyphs[j].y;
                    found = 1;
                    break;
                }
            }

            if (found) {
                for ( ; editor->buffer->cursor_glyph > 0; --(editor->buffer->cursor_glyph)) {
                    double diff  = wantedx - editor->buffer->cursor_line->glyphs[editor->buffer->cursor_glyph].x;
                    if (fabs(cury - editor->buffer->cursor_line->glyphs[editor->buffer->cursor_glyph].y) > 0.001) {
                        ++(editor->buffer->cursor_glyph);
                        break;
                    }
                    if (diff >= 0) {
                        if (fabs(diff) < editor->buffer->em_advance/2) {
                            ++(editor->buffer->cursor_glyph);
                        }
                        break;
                    }
                }
                continue;
            }
            

            if (editor->buffer->cursor_line->prev == NULL) break;
            editor->buffer->cursor_line = editor->buffer->cursor_line->prev;
            if (editor->buffer->cursor_glyph > editor->buffer->cursor_line->cap)
                editor->buffer->cursor_glyph = editor->buffer->cursor_line->cap;
        }
    }

    if ((delta_char != 0) || (special != MOVE_NORMAL)) {
        editor->buffer->cursor_glyph += delta_char;

        if (editor->buffer->cursor_glyph < 0) editor->buffer->cursor_glyph = 0;
        if (editor->buffer->cursor_glyph > editor->buffer->cursor_line->cap) editor->buffer->cursor_glyph = editor->buffer->cursor_line->cap;

        if (special == MOVE_LINE_START) {
            editor->buffer->cursor_glyph = 0;
        } else if (special == MOVE_LINE_END) {
            editor->buffer->cursor_glyph = editor->buffer->cursor_line->cap;
        }
    }

    editor->cursor_visible = TRUE;

    if (should_move_origin) {
        editor_center_on_cursor(editor);
    } else {
        gtk_widget_queue_draw(editor->drar);
    }

    copy_selection_to_clipboard(editor, selection_clipboard);
}

static void text_entry_callback(GtkIMContext *context, gchar *str, gpointer data) {
    editor_t *editor = (editor_t *)data;
    //printf("entered: %s\n", str);

    editor_replace_selection(editor, str);
}

static void insert_paste(editor_t *editor, GtkClipboard *clipboard) {
    gchar *text = gtk_clipboard_wait_for_text(clipboard);
    if (text == NULL) return;

    editor_replace_selection(editor, text);

    g_free(text);
}

static gboolean entry_search_insert_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    int shift = event->state & GDK_SHIFT_MASK;
    int ctrl = event->state & GDK_CONTROL_MASK;
    int alt = event->state & GDK_MOD1_MASK;
    int super = event->state & GDK_SUPER_MASK;

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(editor->entry));
    int len = strlen(text);    
    uint32_t *needle = malloc(len*sizeof(uint32_t));
    int i, dst;
    real_line_t *search_line;
    int search_glyph;
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

    for (i = 0, dst = 0; i < len; ) {
        needle[dst++] = utf8_to_utf32(text, &i, len);
    }

    /*
    printf("Searching [");
    for (i = 0; i < dst; ++i) {
        printf("%d ", needle[i]);
    }
    printf("]\n");*/

    if ((editor->buffer->mark_line == NULL) || editor->search_failed) {
        search_line = editor->buffer->real_line;
        search_glyph = 0;
    } else if (ctrl_g_invoked) {
        search_line = editor->buffer->cursor_line;
        search_glyph = editor->buffer->cursor_glyph;
    } else {
        search_line = editor->buffer->mark_line;
        search_glyph = editor->buffer->mark_glyph;
    }

    for ( ; search_line != NULL; search_line = search_line->next) {
        int i = search_glyph, j = 0;
        search_glyph = 0; // every line searched after the first line will start from the beginning

        for ( ; i < search_line->cap; ++i) {
            if (j >= dst) break;
            if (search_line->glyph_info[i].code == needle[j]) {
                ++j;
            } else {
                i -= j;
                j = 0;
            }
        }

        if (j >= dst) {
            // search was successful            
            editor->buffer->mark_line = search_line;
            editor->buffer->mark_glyph = i - j;
            editor->buffer->cursor_line = search_line;
            editor->buffer->cursor_glyph = i;
            break;
        }
    }

    if (search_line == NULL) {
        editor->search_failed = 0;
        buffer_unset_mark(editor->buffer);
    }
    
    free(needle);

    editor_center_on_cursor(editor);

    if (ctrl_g_invoked) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static void start_search(editor_t *editor) {
    buffer_set_mark_at_cursor(editor->buffer);
    editor->label_state = "search";
    set_label_text(editor);
    gtk_widget_grab_focus(editor->entry);
    g_signal_handler_disconnect(editor->entry, editor->current_entry_handler_id);
    editor->current_entry_handler_id = g_signal_connect(G_OBJECT(editor->entry), "key-release-event", G_CALLBACK(entry_search_insert_callback), editor);
    editor->search_failed = FALSE;
    editor->search_mode = TRUE;
}

void quick_message(editor_t *editor, const char *title, const char *msg) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(editor->window), GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(msg);

    g_signal_connect_swapped(dialog, "response",
                             G_CALLBACK(gtk_widget_destroy), dialog);
    
    gtk_container_add(GTK_CONTAINER(content_area), label);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
}

static gboolean entry_open_insert_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    editor_t *editor = (editor_t*)data;

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_grab_focus(editor->drar);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Return) {
        char *rp;
        buffer_t *buffer = buffers_open(editor->buffer, gtk_entry_get_text(GTK_ENTRY(editor->entry)), &rp);
        if (buffer == NULL) {
            char *msg;
            asprintf(&msg, "Couldn't create or open [%s]", (rp == NULL) ? gtk_entry_get_text(GTK_ENTRY(editor->entry)) : rp);
            quick_message(editor, "Error", msg);
            free(msg);
        } else {
            editor_switch_buffer(editor, buffer);
        }

        if (rp != NULL) free(rp);
        gtk_widget_grab_focus(editor->drar);
        return TRUE;
    }

    //TODO: autocompletion on TAB
    
    return FALSE;
}


static gboolean entry_default_insert_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    enum deferred_action da;

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_grab_focus(editor->drar);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Return) {
        da = interp_eval(editor, gtk_entry_get_text(GTK_ENTRY(editor->entry)));
        switch(da) {
        case CLOSE_EDITOR:
            editor = editors_remove(editor);
            gtk_widget_grab_focus(editor->drar);
            break;
        default:
            gtk_widget_grab_focus(editor->drar);
        } 
    }
    
    //TODO: autocompletion on TAB (here it's complex)
    //TODO: history search on Ctrl-R
    //TODO: history scan with up, down - arrow keys

    return FALSE;
}

void editor_switch_buffer(editor_t *editor, buffer_t *buffer) {
    editor->buffer = buffer;
    set_label_text(editor);
    gtk_widget_queue_draw(editor->drar);
}


static void start_open(editor_t *editor) {
    editor->label_state = "open";
    set_label_text(editor);
    gtk_widget_grab_focus(editor->entry);
    g_signal_handler_disconnect(editor->entry, editor->current_entry_handler_id);
    editor->current_entry_handler_id = g_signal_connect(editor->entry, "key-release-event", G_CALLBACK(entry_open_insert_callback), editor);
}

static gboolean entry_focusout_callback(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    editor->label_state = "cmd";
    set_label_text(editor);
    gtk_entry_set_text(GTK_ENTRY(editor->entry), "");
    editor->search_mode = FALSE;
    g_signal_handler_disconnect(editor->entry, editor->current_entry_handler_id);
    editor->current_entry_handler_id = g_signal_connect(editor->entry, "key-release-event", G_CALLBACK(entry_default_insert_callback), editor);
    return FALSE;
}

static gboolean entry_focusin_callback(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    gtk_entry_set_text(GTK_ENTRY(editor->entry), "");
    return FALSE;
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    int shift = event->state & GDK_SHIFT_MASK;
    int ctrl = event->state & GDK_CONTROL_MASK;
    int alt = event->state & GDK_MOD1_MASK;
    int super = event->state & GDK_SUPER_MASK;

    /* Default key bindings */
    if (!shift && !ctrl && !alt && !super) {
        switch(event->keyval) {
        case GDK_KEY_Up:
            move_cursor(editor, -1, 0, MOVE_NORMAL, TRUE);
            return TRUE;
        case GDK_KEY_Down:
            move_cursor(editor, 1, 0, MOVE_NORMAL, TRUE);
            return TRUE;
        case GDK_KEY_Right:
            move_cursor(editor, 0, 1, MOVE_NORMAL, TRUE);
            return TRUE;
        case GDK_KEY_Left:
            move_cursor(editor, 0, -1, MOVE_NORMAL, TRUE);
            return TRUE;
            
        case GDK_KEY_Page_Up:
            gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) - gtk_adjustment_get_page_increment(GTK_ADJUSTMENT(editor->adjustment)));
            return TRUE;
        case GDK_KEY_Page_Down:
            {
                double nv = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)) + gtk_adjustment_get_page_increment(GTK_ADJUSTMENT(editor->adjustment));
                double mv = gtk_adjustment_get_upper(GTK_ADJUSTMENT(editor->adjustment)) - gtk_adjustment_get_page_size(GTK_ADJUSTMENT(editor->adjustment));
                if (nv > mv) nv = mv;
                gtk_adjustment_set_value(GTK_ADJUSTMENT(editor->adjustment), nv);
                
                return TRUE;
            }
            
        case GDK_KEY_Home:
            move_cursor(editor, 0, 0, MOVE_LINE_START, TRUE);
            return TRUE;
        case GDK_KEY_End:
            move_cursor(editor, 0, 0, MOVE_LINE_END, TRUE);
            return TRUE;

        case GDK_KEY_Tab:
            editor_replace_selection(editor, "\t");
            return TRUE;

        case GDK_KEY_Delete:
            if (editor->buffer->mark_line == NULL) {
                buffer_set_mark_at_cursor(editor->buffer);
                buffer_move_cursor(editor->buffer, +1);
                editor_replace_selection(editor, "");
            } else {
                editor_replace_selection(editor, "");
            }
            return TRUE;
        case GDK_KEY_BackSpace:
            if (editor->buffer->mark_line == NULL) {
                buffer_set_mark_at_cursor(editor->buffer);
                buffer_move_cursor(editor->buffer, -1);
                editor_replace_selection(editor, "");
            } else {
                editor_replace_selection(editor, "");
            }
            return TRUE;
        case GDK_KEY_Return:
            editor_replace_selection(editor, "\n");
            return TRUE;
        }
    }

    /* Temporary default actions */
    if (!shift && ctrl && !alt && !super) {
        switch (event->keyval) {
        case GDK_KEY_space:
            if (editor->buffer->mark_line == NULL) {
                buffer_set_mark_at_cursor(editor->buffer);
            } else {
                buffer_unset_mark(editor->buffer);
                gtk_widget_queue_draw(editor->drar);
            }
            return TRUE;
        case GDK_KEY_c:
            if (editor->buffer->mark_line != NULL) {
                copy_selection_to_clipboard(editor, default_clipboard);
                buffer_unset_mark(editor->buffer);
                gtk_widget_queue_draw(editor->drar);
            }
            return TRUE;
        case GDK_KEY_v:
            insert_paste(editor, default_clipboard);
            return TRUE;
        case GDK_KEY_y:
            insert_paste(editor, selection_clipboard);
            return TRUE;
        case GDK_KEY_x:
            if (editor->buffer->mark_line == NULL) {
                copy_selection_to_clipboard(editor, default_clipboard);
                editor_replace_selection(editor, "");
            }
            return TRUE;
        case GDK_KEY_s:
            save_to_text_file(editor->buffer);
            editor->buffer->modified = 0;
            set_label_text(editor);
            return TRUE;
        case GDK_KEY_f:
            start_search(editor);
            return TRUE;
        case GDK_KEY_o:
            start_open(editor);
            return TRUE;
        case GDK_KEY_b:
            buffers_show_window(editor);
            return TRUE;
        case GDK_KEY_u:
            buffer_undo(editor->buffer);
            return TRUE;
        }
    }

    if (!ctrl && !shift && alt && !super) {
        switch(event->keyval) {
        case GDK_KEY_x:
            gtk_widget_grab_focus(editor->entry);
            return TRUE;
        }
    }

    /* Normal text input processing */
    if (gtk_im_context_filter_keypress(editor->drarim, event)) {
        return TRUE;
    }
    
    printf("Unknown key sequence: %d (shift %d ctrl %d alt %d super %d)\n", event->keyval, shift, ctrl, alt, super);

    return TRUE;
}

static void move_cursor_to_mouse(editor_t *editor, double x, double y) {
    GtkAllocation allocation;
    
    gtk_widget_get_allocation(editor->drar, &allocation);

    x += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment));
    y += gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));
    
    buffer_move_cursor_to_position(editor->buffer, x, y);
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    editor_t *editor = (editor_t *)data;
    gtk_widget_queue_draw(editor->drar);
    gtk_widget_grab_focus(editor->drar);

    move_cursor_to_mouse(editor, event->x, event->y);
    
    editor->cursor_visible = TRUE;

    if (editor->buffer->mark_line != NULL) copy_selection_to_clipboard(editor, selection_clipboard);


    editor->mouse_marking = 1;
    buffer_set_mark_at_cursor(editor->buffer);

    editor_center_on_cursor(editor);
    
    return TRUE;
}

static gboolean button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    editor_t *editor = (editor_t*)data;

    editor->mouse_marking = 0;

    if ((editor->buffer->mark_line == editor->buffer->cursor_line) && (editor->buffer->mark_glyph == editor->buffer->cursor_glyph)) {
        editor->buffer->mark_line = NULL;
        editor->buffer->mark_glyph = -1;
        gtk_widget_queue_draw(editor->drar);
    }

    return TRUE;
}

static gboolean motion_callback(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    if (editor->mouse_marking) {
        move_cursor_to_mouse(editor, event->x, event->y);
        copy_selection_to_clipboard(editor, selection_clipboard);
        editor_center_on_cursor(editor);
    }

    return TRUE;
}

static void draw_selection(editor_t *editor, double width, cairo_t *cr) {
    real_line_t *selstart_line, *selend_line;
    int selstart_glyph, selend_glyph;
    double selstart_y, selend_y;
    double selstart_x, selend_x;
    
    if (editor->buffer->mark_glyph == -1) return;
    
    buffer_get_selection(editor->buffer, &selstart_line, &selstart_glyph, &selend_line, &selend_glyph);

    if ((selstart_line == selend_line) && (selstart_glyph == selend_glyph)) return;

    line_get_glyph_coordinates(editor->buffer, selstart_line, selstart_glyph, &selstart_x, &selstart_y);
    line_get_glyph_coordinates(editor->buffer, selend_line, selend_glyph, &selend_x, &selend_y);

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
}

static gboolean cursor_blinker(editor_t *editor) {
    if (!(editor->initialization_ended)) return TRUE;
    if (editor->cursor_visible < 0) editor->cursor_visible = 1;

    if (!gtk_widget_is_focus(editor->drar)) {
        if (!(editor->cursor_visible)) editor->cursor_visible = 1;
    } else {
        editor->cursor_visible = (editor->cursor_visible + 1) % 3;
    }

    gtk_widget_queue_draw(editor->drar);
    
    return TRUE;
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    editor_t *editor = (editor_t*)data;
    cairo_t *cr = gdk_cairo_create(widget->window);
    GtkAllocation allocation;
    double originy;
    real_line_t *line;
    int mark_mode = 0;
    int count = 0;
    int drawn_lines = 0;

    gtk_widget_get_allocation(widget, &allocation);

    if (!(editor->initialization_ended)) {
        gdk_window_set_cursor(gtk_widget_get_window(editor->drar), gdk_cursor_new(GDK_XTERM));
        editor->timeout_id = g_timeout_add(500, (GSourceFunc)cursor_blinker, (gpointer)editor);
        editor->initialization_ended = 1;
    }

    /*printf("%dx%d +%dx%d (%dx%d)\n", event->area.x, event->area.y, event->area.width, event->area.height, allocation.width, allocation.height);*/

    cairo_set_source_rgb(cr, 0, 0, 1.0);
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_scaled_font(cr, editor->buffer->main_font.cairofont);

    cairo_translate(cr, -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), -gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));

    /*printf("DRAWING!\n");*/

    buffer_typeset_maybe(editor->buffer, allocation.width);

    editor->buffer->rendered_height = 0.0;

    originy = gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment));

    for (line = editor->buffer->real_line; line != NULL; line = line->next) {
        int start_selection_at_glyph = -1, end_selection_at_glyph = -1;
        int i;
        double cury;

        if (line == editor->buffer->mark_line) {
            if (mark_mode) {
                end_selection_at_glyph = editor->buffer->mark_glyph;
                mark_mode = 0;
            } else {
                start_selection_at_glyph = editor->buffer->mark_glyph;
                mark_mode = 1;
            }
        }

        if (mark_mode || (editor->buffer->mark_line != NULL)) {
            if (line == editor->buffer->cursor_line) {
                if (mark_mode) {
                    end_selection_at_glyph = editor->buffer->cursor_glyph;
                    mark_mode = 0;
                } else {
                    start_selection_at_glyph = editor->buffer->cursor_glyph;
                    mark_mode = 1;
                }
            }
        }

        //buffer_line_adjust_glyphs(editor->buffer, line, origin_x, y, allocation.width, allocation.height, &y_increment, &line_end_width);

        if (((line->start_y + line->y_increment - originy) > 0) && ((line->start_y - originy) < allocation.height)) {
            cairo_show_glyphs(cr, line->glyphs, line->cap);

            cury = line->start_y;

            for (i = 0; i < line->cap; ++i) {
                if (line->glyphs[i].y - cury > 0.001) {
                    /* draw ending tract */
                    cairo_set_line_width(cr, 4.0);
                    cairo_move_to(cr, line->glyphs[i-1].x + line->glyph_info[i-1].x_advance, cury-(editor->buffer->ex_height/2.0));
                    cairo_line_to(cr, allocation.width, cury-(editor->buffer->ex_height/2.0));
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
            ++drawn_lines;
        }

        editor->buffer->rendered_height += line->y_increment;

        ++count;
    }

    draw_selection(editor, allocation.width, cr);

    //printf("Drawn lines: %d\n", drawn_lines);

    //printf("Expose event final y: %g, lines: %d\n", y, count);

    if (editor->cursor_visible && !(editor->search_mode)) {
        double cursor_x, cursor_y;
        
        buffer_cursor_position(editor->buffer, &cursor_x, &cursor_y);
        
        /*cairo_set_source_rgb(cr, 119.0/255, 136.0/255, 153.0/255);*/
        cairo_rectangle(cr, cursor_x, cursor_y-editor->buffer->ascent, 2, editor->buffer->ascent+editor->buffer->descent);
        cairo_fill(cr);
    }

    /********** NOTHING IS TRANSLATED BEYOND THIS ***************************/
    cairo_translate(cr, gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->hadjustment)), gtk_adjustment_get_value(GTK_ADJUSTMENT(editor->adjustment)));
    

    {
        char *posbox_text;
        asprintf(&posbox_text, " %d,%d %0.0f%%", editor->buffer->cursor_line->lineno, editor->buffer->cursor_glyph, (100.0 * editor->buffer->cursor_line->lineno / count));
        cairo_text_extents_t posbox_ext;
        double x, y;

        cairo_set_scaled_font(cr, editor->buffer->posbox_font.cairofont);

        cairo_text_extents(cr, posbox_text, &posbox_ext);

        y = allocation.height - posbox_ext.height - 4.0;
        x = allocation.width - posbox_ext.x_advance - 4.0;

        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_rectangle(cr, x-1.0, y-1.0, posbox_ext.x_advance+4.0, posbox_ext.height+4.0);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 238.0/255, 221.0/255, 130.0/255);
        cairo_rectangle(cr, x, y, posbox_ext.x_advance + 2.0, posbox_ext.height + 2.0);
        cairo_fill(cr);

        cairo_move_to(cr, x+1.0, y+posbox_ext.height);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_show_text(cr, posbox_text);
        
        free(posbox_text);
    }

    gtk_adjustment_set_upper(GTK_ADJUSTMENT(editor->adjustment), editor->buffer->rendered_height);
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

editor_t *new_editor(GtkWidget *window, buffer_t *buffer) {
    editor_t *r = malloc(sizeof(editor_t));

    r->window = window;
    r->buffer = buffer;
    r->cursor_visible = TRUE;
    r->initialization_ended = 0;
    r->mouse_marking = 0;

    r->buffer->modified = 0;
    
    r->search_mode = FALSE;
    r->search_failed = FALSE;

    r->drar = gtk_drawing_area_new();
    r->drarim = gtk_im_multicontext_new();

    r->timeout_id = -1;

    gtk_widget_add_events(r->drar, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    gtk_widget_set_can_focus(GTK_WIDGET(r->drar), TRUE);

    g_signal_connect(G_OBJECT(r->drar), "expose_event",
                     G_CALLBACK(expose_event_callback), r);

    g_signal_connect(G_OBJECT(r->drar), "key-press-event",
                     G_CALLBACK(key_press_callback), r);

    g_signal_connect(G_OBJECT(r->drar), "button-press-event",
                     G_CALLBACK(button_press_callback), r);

    g_signal_connect(G_OBJECT(r->drar), "button-release-event",
                     G_CALLBACK(button_release_callback), r);

    g_signal_connect(G_OBJECT(r->drar), "motion-notify-event",
                     G_CALLBACK(motion_callback), r);

    g_signal_connect(G_OBJECT(r->drarim), "commit",
                     G_CALLBACK(text_entry_callback), r);

    {
        GtkWidget *drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(r->adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        r->drarhscroll = gtk_hscrollbar_new((GtkAdjustment *)(r->hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        GtkWidget *tag = gtk_hbox_new(FALSE, 2);

        r->table = gtk_table_new(2, 2, FALSE);
        
        r->label = gtk_label_new("");
        r->entry = gtk_entry_new();

        r->current_entry_handler_id = g_signal_connect(r->entry, "key-release-event", G_CALLBACK(entry_default_insert_callback), r);

        r->label_state = "cmd";
        set_label_text(r);

        g_signal_connect(r->entry, "focus-out-event", G_CALLBACK(entry_focusout_callback), r);
        g_signal_connect(r->entry, "focus-in-event", G_CALLBACK(entry_focusin_callback), r);

        gtk_container_add(GTK_CONTAINER(tag), r->label);
        gtk_container_add(GTK_CONTAINER(tag), r->entry);

        gtk_box_set_child_packing(GTK_BOX(tag), r->label, FALSE, FALSE, 2, GTK_PACK_START);
        gtk_box_set_child_packing(GTK_BOX(tag), r->entry, TRUE, TRUE, 2, GTK_PACK_END);

        gtk_table_attach(GTK_TABLE(r->table), tag, 0, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 1, 1);
        gtk_table_attach(GTK_TABLE(r->table), r->drar, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 1, 1);
        gtk_table_attach(GTK_TABLE(r->table), drarscroll, 1, 2, 1, 2, 0, GTK_EXPAND|GTK_FILL, 1, 1);
        gtk_table_attach(GTK_TABLE(r->table), r->drarhscroll, 0, 1, 2, 3, GTK_EXPAND|GTK_FILL, 0, 1, 1);


        g_signal_connect(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), (gpointer)r);
        g_signal_connect(G_OBJECT(r->drarhscroll), "value_changed", G_CALLBACK(hscrolled_callback), (gpointer)r);
    }

    r->allocated_vertical_space = 0;

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
    //gtk_widget_destroy(editor->table);
    free(editor);
}

