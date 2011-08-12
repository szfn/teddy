/* bla */
/* reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes  reallly long line for testing purposes */
#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <gdk/gdkkeysyms.h>
#include <assert.h>
#include <math.h>

#include "buffer.h"

FT_Library library;

buffer_t *buffer;

GtkClipboard *selection_clipboard;
GtkClipboard *default_clipboard;
GtkObject *adjustment, *hadjustment;
GtkWidget *drar;
GtkWidget *drarhscroll;
GtkIMContext *drarim;
gboolean cursor_visible = TRUE;
int initialization_ended = 0;
int mouse_marking = 0;

static double calculate_x_origin(GtkAllocation *allocation) {
    double origin_x;

    if (allocation->width > buffer->rendered_width) {
        /* if there is enough space to show everything then there is no need to use the scrollbar */
        origin_x = buffer->left_margin;
    } else {
        origin_x = buffer->left_margin - gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment));
    }

    return origin_x;
}

static double calculate_y_origin(GtkAllocation *allocation) {
    double origin_y;

    if (allocation->height > buffer->rendered_height) {
        /* if there is enough space to show everything then there is no need to use the scrollbar */
        origin_y = buffer->line_height;
    } else {
        origin_y = buffer->line_height - gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment));
    }

    return origin_y;
}

static void redraw_cursor_line(gboolean large, gboolean move_origin_when_outside) {
    double origin_x, origin_y;
    double cursor_x, x, y, height, width;
    GtkAllocation allocation;

    if (!initialization_ended) return;

    gtk_widget_get_allocation(drar, &allocation);

    origin_x = calculate_x_origin(&allocation);
    origin_y = calculate_y_origin(&allocation);

    buffer_cursor_position(buffer, &cursor_x, &y);

    y -= buffer->ascent;
    x = 0.0;
    height = buffer->ascent + buffer->descent;
    width = allocation.width;

    if (large) {
        y -= buffer->line_height;
        height += 2*buffer->line_height;
    }

    if (move_origin_when_outside) {
        if (y+height > allocation.height) {
            double diff = y+height - allocation.height;
            gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)) + diff);
            gtk_widget_queue_draw(drar);
            return;
        }

        if (y < 0.0) {
            gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)) + y);
            gtk_widget_queue_draw(drar);
            return;
        }

        if (cursor_x + buffer->em_advance > allocation.width) {
            double diff = cursor_x + buffer->em_advance - allocation.width;
            gtk_adjustment_set_value(GTK_ADJUSTMENT(hadjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment)) + diff);
            gtk_widget_queue_draw(drar);
            return;
        }

        if (cursor_x - buffer->left_margin < 0.0) {
            gtk_adjustment_set_value(GTK_ADJUSTMENT(hadjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment)) + cursor_x - buffer->left_margin);
            gtk_widget_queue_draw(drar);
            return;
        }
    }

    if (buffer->mark_lineno != -1) {
        gtk_widget_queue_draw(drar);
    } else {
        gtk_widget_queue_draw_area(drar, x, y, width, height);
        gtk_widget_queue_draw_area(drar, 0.0, allocation.height-buffer->line_height, allocation.width, buffer->line_height);
    }
}

static void copy_selection_to_clipboard(GtkClipboard *clipboard) {
    real_line_t *start_line, *end_line;
    int start_glyph, end_glyph;
    char *r = NULL;
    

    buffer_get_selection(buffer, &start_line, &start_glyph, &end_line, &end_glyph);
 
    if (start_line == NULL) return;
    if (end_line == NULL) return;

    r  = buffer_lines_to_text(buffer, start_line, end_line, start_glyph, end_glyph);    

    gtk_clipboard_set_text(clipboard, r, -1);

    free(r);
}

static void remove_selection() {
    real_line_t *start_line, *end_line, *real_line;
    int start_glyph, end_glyph;
    int lineno;

    buffer_get_selection(buffer, &start_line, &start_glyph, &end_line, &end_glyph);
 
    if (start_line == NULL) return;
    if (end_line == NULL) return;

    //printf("Deleting %d from %d (size: %d)\n", start_line->lineno, start_glyph, start_line->cap-start_glyph);

    /* Special case when we are deleting a section of the same line */
    if (start_line == end_line) {
        buffer_line_delete_from(buffer, start_line, start_glyph, end_glyph-start_glyph);
        buffer_set_to_real(buffer, start_line, start_glyph);
        gtk_widget_queue_draw(drar);
        return;
    }
    
    /* Remove text from first and last real lines */
    buffer_line_delete_from(buffer, start_line, start_glyph, start_line->cap-start_glyph);
    buffer_line_delete_from(buffer, end_line, 0, end_glyph);

    /* Remove real_lines between start and end */
    for (real_line = start_line->next; (real_line != NULL) && (real_line != end_line); ) {
        real_line_t *next = real_line->next;
        free(real_line->glyphs);
        free(real_line->glyph_info);
        free(real_line);
        real_line = next;
    }

    start_line->next = end_line;
    end_line->prev = start_line;

    /* Renumber real_lines */

    lineno = start_line->lineno+1;
    for (real_line = start_line->next; real_line != NULL; real_line = real_line->next) {
        real_line->lineno = lineno;
        ++lineno;
    }

    /*
    printf("AFTER FIXING REAL LINES, BEFORE FIXING DISPLAY LINES:\n");
    debug_print_real_lines_state(buffer);
    debug_print_lines_state(buffer);
    */

    buffer_set_to_real(buffer, start_line, start_glyph);

    buffer_join_lines(buffer, start_line, end_line);

    gtk_widget_queue_draw(drar);
}

enum MoveCursorSpecial {
    MOVE_NORMAL = 1,
    MOVE_LINE_START,
    MOVE_LINE_END,
};

static void move_cursor(int delta_line, int delta_char, enum MoveCursorSpecial special) {
    int i = 0;
    redraw_cursor_line(FALSE, FALSE);

    if (delta_line > 0) {
        for (i = 0; i < delta_line; ++i) {
            if (buffer->cursor_glyph < buffer->cursor_line->cap) {
                int j = 0;
                int found = 0;
                double cury = buffer->cursor_line->glyphs[buffer->cursor_glyph].y;
                double wantedx = buffer->cursor_line->glyphs[buffer->cursor_glyph].x;
                for (j = buffer->cursor_glyph; j < buffer->cursor_line->cap; ++j) {
                    if (fabs(cury - buffer->cursor_line->glyphs[j].y) > 0.001) {
                        //printf("Actually found %g %g\n", cury, buffer->cursor_line->glyphs[j].y);
                        buffer->cursor_glyph = j;
                        cury = buffer->cursor_line->glyphs[j].y;
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    for ( ; buffer->cursor_glyph < buffer->cursor_line->cap; ++(buffer->cursor_glyph)) {
                        double diff = wantedx - buffer->cursor_line->glyphs[buffer->cursor_glyph].x;
                        if (fabs(cury - buffer->cursor_line->glyphs[buffer->cursor_glyph].y) > 0.001) {
                            --(buffer->cursor_glyph);
                            break;
                        }

                        if (diff <= 0) {
                            if (fabs(diff) > buffer->em_advance/2) {
                                --(buffer->cursor_glyph);
                            }
                            break;
                        }
                    }
                    continue;
                }
            }

            if (buffer->cursor_line->next == NULL) break;
            buffer->cursor_line = buffer->cursor_line->next;
            if (buffer->cursor_glyph > buffer->cursor_line->cap)
                buffer->cursor_glyph = buffer->cursor_line->cap;
        }
    }

    if (delta_line < 0) {
        for (i = 0; i < abs(delta_line); ++i) {
            int j = 0;
            int found = 0;
            double cury, wantedx;
            line_get_glyph_coordinates(buffer, buffer->cursor_line, buffer->cursor_glyph, &wantedx, &cury);
            for (j = buffer->cursor_glyph-1; j >= 0; --j) {
                if (fabs(cury - buffer->cursor_line->glyphs[j].y) > 0.001) {
                    buffer->cursor_glyph = j;
                    cury = buffer->cursor_line->glyphs[j].y;
                    found = 1;
                    break;
                }
            }

            if (found) {
                for ( ; buffer->cursor_glyph > 0; --(buffer->cursor_glyph)) {
                    double diff  = wantedx - buffer->cursor_line->glyphs[buffer->cursor_glyph].x;
                    if (fabs(cury - buffer->cursor_line->glyphs[buffer->cursor_glyph].y) > 0.001) {
                        ++(buffer->cursor_glyph);
                        break;
                    }
                    if (diff >= 0) {
                        if (fabs(diff) < buffer->em_advance/2) {
                            ++(buffer->cursor_glyph);
                        }
                        break;
                    }
                }
                continue;
            }
            

            if (buffer->cursor_line->prev == NULL) break;
            buffer->cursor_line = buffer->cursor_line->prev;
            if (buffer->cursor_glyph > buffer->cursor_line->cap)
                buffer->cursor_glyph = buffer->cursor_line->cap;
        }
    }

    if ((delta_char != 0) || (special != MOVE_NORMAL)) {
        buffer->cursor_glyph += delta_char;

        if (buffer->cursor_glyph < 0) buffer->cursor_glyph = 0;
        if (buffer->cursor_glyph > buffer->cursor_line->cap) buffer->cursor_glyph = buffer->cursor_line->cap;

        if (special == MOVE_LINE_START) {
            buffer->cursor_glyph = 0;
        } else if (special == MOVE_LINE_END) {
            buffer->cursor_glyph = buffer->cursor_line->cap;
        }
    }

    cursor_visible = TRUE;

    redraw_cursor_line(FALSE, TRUE);

    copy_selection_to_clipboard(selection_clipboard);
}

static void unset_mark(void) {
    if (buffer->mark_lineno != -1) {
        buffer->mark_lineno = -1;
        buffer->mark_glyph = -1;
        printf("Mark unset\n");
        gtk_widget_queue_draw(drar);
    }
}

static void insert_text(gchar *str) {
    int inc = buffer_line_insert_utf8_text(buffer, buffer->cursor_line, str, strlen(str), buffer->cursor_glyph);

    unset_mark();

    move_cursor(0, inc, MOVE_NORMAL);
    
    redraw_cursor_line(FALSE, TRUE);
}

static void remove_text(int offset) {
    real_line_t *real_cursor_line, *prev_cursor_line;
    int real_cursor_glyph;
    int prev_cursor_line_cap = 0;

    unset_mark();

    buffer_real_cursor(buffer, &real_cursor_line, &real_cursor_glyph);
    prev_cursor_line = real_cursor_line->prev;
    if (prev_cursor_line != NULL) {
        prev_cursor_line_cap = prev_cursor_line->cap;
    }

    buffer_line_remove_glyph(buffer, buffer->cursor_line, buffer->cursor_glyph + offset);

    real_cursor_glyph += offset;
    if (real_cursor_glyph < 0) {
        /* real_cursor_line may have been deleted by a line join */
        if (prev_cursor_line != NULL) {
            real_cursor_line = prev_cursor_line;
            real_cursor_glyph = prev_cursor_line_cap;
        } else {
            /* it wasn't deleted because this is the first line */
            real_cursor_glyph = 0;
        }
    }
    
    buffer_set_to_real(buffer, real_cursor_line, real_cursor_glyph);

    gtk_widget_queue_draw(drar);
}

static void split_line() {
    unset_mark();

    buffer_split_line(buffer, buffer->cursor_line, buffer->cursor_glyph);
    assert(buffer->cursor_line->next != NULL);
    buffer->cursor_line = buffer->cursor_line->next;
    buffer->cursor_glyph = 0;

    redraw_cursor_line(FALSE, TRUE);

    gtk_widget_queue_draw(drar);
}

static void text_entry_callback(GtkIMContext *context, gchar *str, gpointer data) {
    //printf("entered: %s\n", str);

    insert_text(str);
}

static void set_mark_at_cursor(void) {
    real_line_t *cursor_real_line;
    int cursor_real_glyph;
    buffer_real_cursor(buffer, &cursor_real_line, &cursor_real_glyph);
    buffer->mark_lineno = cursor_real_line->lineno;
    buffer->mark_glyph = cursor_real_glyph;
    printf("Mark set @ %d,%d\n", buffer->mark_lineno, buffer->mark_glyph);
}

static void insert_paste(GtkClipboard *clipboard) {
    gchar *text = gtk_clipboard_wait_for_text(clipboard);
    if (text == NULL) return;

    buffer_insert_multiline_text(buffer, buffer->cursor_line, buffer->cursor_glyph, text);
    gtk_widget_queue_draw(drar);
    
    g_free(text);
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    int shift = event->state & GDK_SHIFT_MASK;
    int ctrl = event->state & GDK_CONTROL_MASK;
    int alt = event->state & GDK_MOD1_MASK;
    int super = event->state & GDK_SUPER_MASK;

    /* Default key bindings */
    if (!shift && !ctrl && !alt && !super) {
        switch(event->keyval) {
        case GDK_KEY_Up:
            move_cursor(-1, 0, MOVE_NORMAL);
            return TRUE;
        case GDK_KEY_Down:
            move_cursor(1, 0, MOVE_NORMAL);
            return TRUE;
        case GDK_KEY_Right:
            move_cursor(0, 1, MOVE_NORMAL);
            return TRUE;
        case GDK_KEY_Left:
            move_cursor(0, -1, MOVE_NORMAL);
            return TRUE;
            
        case GDK_KEY_Page_Up:
            gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustment), gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)) - gtk_adjustment_get_page_increment(GTK_ADJUSTMENT(adjustment)));
            return TRUE;
        case GDK_KEY_Page_Down:
            {
                double nv = gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)) + gtk_adjustment_get_page_increment(GTK_ADJUSTMENT(adjustment));
                double mv = gtk_adjustment_get_upper(GTK_ADJUSTMENT(adjustment)) - gtk_adjustment_get_page_size(GTK_ADJUSTMENT(adjustment));
                if (nv > mv) nv = mv;
                gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustment), nv);
                
                return TRUE;
            }
            
        case GDK_KEY_Home:
            move_cursor(0, 0, MOVE_LINE_START);
            return TRUE;
        case GDK_KEY_End:
            move_cursor(0, 0, MOVE_LINE_END);
            return TRUE;

        case GDK_KEY_Tab:
            insert_text("\t");
            return TRUE;

        case GDK_KEY_Delete:
            if (buffer->mark_lineno == -1) {
                remove_text(0);
            } else {
                remove_selection();
                unset_mark();
            }
            return TRUE;
        case GDK_KEY_BackSpace:
            if (buffer->mark_lineno == -1) {
                remove_text(-1);
            } else {
                remove_selection();
                unset_mark();
            }
            return TRUE;
        case GDK_KEY_Return:
            split_line();
            return TRUE;
        }
    }

    /* Temporary default actions */
    if (!shift && ctrl && !alt) {
        switch (event->keyval) {
        case GDK_KEY_space:
            if (buffer->mark_lineno == -1) {
                set_mark_at_cursor();
            } else {
                unset_mark();
            }
            return TRUE;
        case GDK_KEY_c:
            if (buffer->mark_lineno != -1) {
                copy_selection_to_clipboard(default_clipboard);
                unset_mark();
            }
            return TRUE;
        case GDK_KEY_v:
            if (buffer->mark_lineno != -1) {
                remove_selection();
                unset_mark();
            }
            insert_paste(default_clipboard);
            return TRUE;
        case GDK_KEY_y:
            if (buffer->mark_lineno != -1) {
                remove_selection();
                unset_mark();
            }
            insert_paste(selection_clipboard);
            return TRUE;
        case GDK_KEY_x:
            if (buffer->mark_lineno != -1) {
                copy_selection_to_clipboard(default_clipboard);
                remove_selection();
                unset_mark();
            }
            return TRUE;
        case GDK_KEY_s:
            save_to_text_file(buffer);
            return TRUE;
        }
    }

    /* Normal text input processing */
    if (gtk_im_context_filter_keypress(drarim, event)) {
        return TRUE;
    }
    
    printf("Unknown key sequence: %d (shift %d ctrl %d alt %d super %d)\n", event->keyval, shift, ctrl, alt, super);

    return TRUE;
}

static void move_cursor_to_mouse(double x, double y) {
    double origin_x, origin_y;
    GtkAllocation allocation;
    
    gtk_widget_get_allocation(drar, &allocation);
    
    origin_y = calculate_y_origin(&allocation);
    origin_x = calculate_x_origin(&allocation);
    
    buffer_move_cursor_to_position(buffer, x, y);
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    redraw_cursor_line(FALSE, FALSE);

    move_cursor_to_mouse(event->x, event->y);
    
    cursor_visible = TRUE;

    if (buffer->mark_lineno != -1) {
        gtk_widget_queue_draw(drar); /* must redraw everything to keep selection consistent */
        
        copy_selection_to_clipboard(selection_clipboard);
    } else {
        redraw_cursor_line(FALSE, FALSE);
    }

    mouse_marking = 1;
    set_mark_at_cursor();
    
    return TRUE;
}

static gboolean button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    real_line_t *cursor_real_line;
    int cursor_real_glyph;

    buffer_real_cursor(buffer, &cursor_real_line, &cursor_real_glyph);
    
    mouse_marking = 0;

    if ((buffer->mark_lineno == cursor_real_line->lineno) && (buffer->mark_glyph == cursor_real_glyph)) {
        buffer->mark_lineno = -1;
        buffer->mark_glyph = -1;
        redraw_cursor_line(FALSE, FALSE);
    }

    return TRUE;
}

static gboolean motion_callback(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (mouse_marking) {
        move_cursor_to_mouse(event->x, event->y);
        copy_selection_to_clipboard(selection_clipboard);
        gtk_widget_queue_draw(drar);
    }

    return TRUE;
}

static void draw_selection(double width, cairo_t *cr) {
    real_line_t *selstart_line, *selend_line;
    int selstart_glyph, selend_glyph;
    double selstart_y, selend_y;
    double selstart_x, selend_x;
    
    if (buffer->mark_glyph == -1) return;
    
    buffer_get_selection(buffer, &selstart_line, &selstart_glyph, &selend_line, &selend_glyph);

    if ((selstart_line == selend_line) && (selstart_glyph == selend_glyph)) return;

    line_get_glyph_coordinates(buffer, selstart_line, selstart_glyph, &selstart_x, &selstart_y);
    line_get_glyph_coordinates(buffer, selend_line, selend_glyph, &selend_x, &selend_y);

    if (fabs(selstart_y - selend_y) < 0.001) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_rectangle(cr, selstart_x, selstart_y-buffer->ascent, selend_x - selstart_x, buffer->ascent + buffer->descent);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    } else {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);

        // start of selection
        cairo_rectangle(cr, selstart_x, selstart_y-buffer->ascent, width - selstart_x, buffer->ascent + buffer->descent);
        cairo_fill(cr);

        // middle of selection
        cairo_rectangle(cr, 0.0, selstart_y + buffer->descent, width, selend_y - buffer->ascent - buffer->descent - selstart_y);
        cairo_fill(cr);

        // end of selection
        cairo_rectangle(cr, 0.0, selend_y-buffer->ascent, selend_x, buffer->ascent + buffer->descent);
        cairo_fill(cr);
        
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    double origin_y, origin_x, y;
    GtkAllocation allocation;
    real_line_t *line, *first_displayed_line = NULL;
    int first_displayed_glyph = -1;
    int mark_mode = 0;
    int display_lines = 0;
    int count = 0;

    gtk_widget_get_allocation(widget, &allocation);

    /*printf("%dx%d +%dx%d (%dx%d)\n", event->area.x, event->area.y, event->area.width, event->area.height, allocation.width, allocation.height);*/

    cairo_set_source_rgb(cr, 0, 0, 1.0);
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_scaled_font(cr, buffer->main_font.cairofont);

    origin_y = calculate_y_origin(&allocation);
    origin_x = calculate_x_origin(&allocation);

    y = origin_y;

    /*printf("DRAWING!\n");*/

    for (line = buffer->real_line; line != NULL; line = line->next) {
        double line_end_width, y_increment;
        int start_selection_at_glyph = -1, end_selection_at_glyph = -1;
        int i;
        double cury;

        if (line->lineno == buffer->mark_lineno) {
            if (mark_mode) {
                end_selection_at_glyph = buffer->mark_glyph;
                mark_mode = 0;
            } else {
                start_selection_at_glyph = buffer->mark_glyph;
                mark_mode = 1;
            }
        }

        if (mark_mode || (buffer->mark_lineno != -1)) {
            if (line == buffer->cursor_line) {
                if (mark_mode) {
                    end_selection_at_glyph = buffer->cursor_glyph;
                    mark_mode = 0;
                } else {
                    start_selection_at_glyph = buffer->cursor_glyph;
                    mark_mode = 1;
                }
            }
        }

        buffer_line_adjust_glyphs(buffer, line, origin_x, y, allocation.width, allocation.height, &y_increment, &line_end_width);
        cairo_show_glyphs(cr, line->glyphs, line->cap);

        cury = line->start_y;

        /* line may not have any glyphs but could still be first displayed line */
        if ((first_displayed_line == NULL) && (y + buffer->line_height > 0)) {
            first_displayed_line = line;
            first_displayed_glyph = 0;
        }
        
        for (i = 0; i < line->cap; ++i) {
            if (line->glyphs[i].y - cury > 0.001) {
                if ((first_displayed_line == NULL) && (y + buffer->line_height > 0)) {
                    first_displayed_line = line;
                    first_displayed_glyph = i;
                }

                /* draw ending tract */
                cairo_set_line_width(cr, 4.0);
                cairo_move_to(cr, line->glyphs[i-1].x + line->glyph_info[i-1].x_advance, cury-(buffer->ex_height/2.0));
                cairo_line_to(cr, allocation.width, cury-(buffer->ex_height/2.0));
                cairo_stroke(cr);
                cairo_set_line_width(cr, 2.0);

                cury = line->glyphs[i].y;

                /* draw initial tract */
                cairo_set_line_width(cr, 4.0);
                cairo_move_to(cr, 0.0, cury-(buffer->ex_height/2.0));
                cairo_line_to(cr, buffer->left_margin, cury-(buffer->ex_height/2.0));
                cairo_stroke(cr);
                cairo_set_line_width(cr, 2.0);
            }
        }

        y += y_increment;
        ++count;
    }

    draw_selection(allocation.width, cr);

    //printf("Expose event final y: %g, lines: %d\n", y, count);

    if (cursor_visible) {
        double cursor_x, cursor_y;

        buffer_cursor_position(buffer, &cursor_x, &cursor_y);

        if ((cursor_y < 0) || (cursor_y > allocation.height)) {
            if (first_displayed_line != NULL) {
                buffer->cursor_line = first_displayed_line;
                buffer->cursor_glyph = first_displayed_glyph;
                redraw_cursor_line(FALSE, FALSE);
            }
        } else {
            /*cairo_set_source_rgb(cr, 119.0/255, 136.0/255, 153.0/255);*/
            cairo_rectangle(cr, cursor_x, cursor_y-buffer->ascent, 2, buffer->ascent+buffer->descent);
            cairo_fill(cr);
        }
    }

    {
        char *posbox_text;
        asprintf(&posbox_text, " %d,%d %0.0f%%", buffer->cursor_line->lineno, buffer->cursor_glyph, (100.0 * display_lines));
        cairo_text_extents_t posbox_ext;
        double x, y;

        cairo_set_scaled_font(cr, buffer->posbox_font.cairofont);

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

    buffer->rendered_height = y - origin_y;

    gtk_adjustment_set_upper(GTK_ADJUSTMENT(adjustment), buffer->rendered_height);
    gtk_adjustment_set_page_size(GTK_ADJUSTMENT(adjustment), allocation.height);
    gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(adjustment), allocation.height/2);
    gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(adjustment), buffer->line_height);

    if (buffer->rendered_width <= allocation.width) {
        gtk_widget_hide(GTK_WIDGET(drarhscroll));
    } else {
        gtk_widget_show(GTK_WIDGET(drarhscroll));
        gtk_adjustment_set_upper(GTK_ADJUSTMENT(hadjustment), buffer->left_margin + buffer->rendered_width);
        gtk_adjustment_set_page_size(GTK_ADJUSTMENT(hadjustment), allocation.width);
        gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(hadjustment), allocation.width/2);
        gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(hadjustment), buffer->em_advance);
    }

    cairo_destroy(cr);

    initialization_ended = 1;
  
    return TRUE;
}

static gboolean scrolled_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
    /*printf("Scrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)));*/
    gtk_widget_queue_draw(drar);
    return TRUE;
}

static gboolean hscrolled_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
    /* printf("HScrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment)));*/
    gtk_widget_queue_draw(drar);
    return TRUE;
}

static gboolean cursor_blinker(GtkWidget *widget) {
    if (!initialization_ended) return TRUE;
    if (cursor_visible < 0) cursor_visible = 1;

    cursor_visible = (cursor_visible + 1) % 3;

    redraw_cursor_line(FALSE, FALSE);

    return TRUE;
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    int error;

    gtk_init(&argc, &argv);

    error = FT_Init_FreeType(&library);
    if (error) {
        printf("Freetype initialization error\n");
        exit(EXIT_FAILURE);
    }

    if (argc <= 1) {
        printf("Nothing to show\n");
        exit(EXIT_SUCCESS);
    }

    printf("Will show: %s\n", argv[1]);

    buffer = buffer_create(&library);

    load_text_file(buffer, argv[1]);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "Acmacs");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 600);

    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    drar = gtk_drawing_area_new();
    drarim = gtk_im_multicontext_new();
    selection_clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    default_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    gtk_widget_set_can_focus(GTK_WIDGET(drar), TRUE);

    g_signal_connect(G_OBJECT(drar), "expose_event",
                     G_CALLBACK(expose_event_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "key-press-event",
                     G_CALLBACK(key_press_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "button-press-event",
                     G_CALLBACK(button_press_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "button-release-event",
                     G_CALLBACK(button_release_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "motion-notify-event",
                     G_CALLBACK(motion_callback), NULL);

    g_signal_connect(G_OBJECT(drarim), "commit",
                     G_CALLBACK(text_entry_callback), NULL);

    {
        GtkWidget *drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        drarhscroll = gtk_hscrollbar_new((GtkAdjustment *)(hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        GtkWidget *table = gtk_table_new(2, 2, FALSE);

        gtk_table_attach(GTK_TABLE(table), drar, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 1, 1);
        gtk_table_attach(GTK_TABLE(table), drarscroll, 1, 2, 0, 1, 0, GTK_EXPAND|GTK_FILL, 1, 1);
        gtk_table_attach(GTK_TABLE(table), drarhscroll, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 1, 1);

        gtk_container_add(GTK_CONTAINER(window), table);

        g_signal_connect(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), NULL);
        g_signal_connect(G_OBJECT(drarhscroll), "value_changed", G_CALLBACK(hscrolled_callback), NULL);
    }

    gtk_widget_show_all(window);

    gtk_widget_grab_focus(GTK_WIDGET(drar));
    gdk_window_set_events(gtk_widget_get_window(drar), gdk_window_get_events(gtk_widget_get_window(drar)) | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_timeout_add(500, (GSourceFunc)cursor_blinker, (gpointer)window);

    gtk_main();

    initialization_ended = 0;

    buffer_free(buffer);

    return 0;
}
