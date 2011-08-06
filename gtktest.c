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

#include "buffer.h"

FT_Library library;

buffer_t *buffer;

GtkObject *adjustment, *hadjustment;
GtkWidget *drar;
GtkWidget *drarhscroll;
GtkIMContext *drarim;
gboolean cursor_visible = TRUE;

/* TODO:
   - rimpiazzare array delle linee con una linked list (cosi` e` impossibile farlo funzionare)
   - soft line wrap
   - editing
   - key bindings
   - highlighting
*/

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

    gtk_widget_get_allocation(drar, &allocation);

    origin_x = calculate_x_origin(&allocation);
    origin_y = calculate_y_origin(&allocation);

    buffer_cursor_position(buffer, origin_x, origin_y, &cursor_x, &y);

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
    
    gtk_widget_queue_draw_area(drar, x, y, width, height);
    gtk_widget_queue_draw_area(drar, 0.0, allocation.height-buffer->line_height, allocation.width, buffer->line_height);
}

static void move_cursor(int delta_line, int delta_char) {
    redraw_cursor_line(FALSE, FALSE);
    /* TODO: reimplement
    buffer->cursor_line += delta_line;

    if (buffer->cursor_line >= buffer->lines_cap) buffer->cursor_line = buffer->lines_cap-1;
    if (buffer->cursor_line < 0) buffer->cursor_line = 0;    

    buffer->cursor_glyph += delta_char;

    if (buffer->cursor_line >= buffer->lines_cap) buffer->cursor_glyph = 0; // only happens when there are no lines
    if (buffer->cursor_glyph > buffer->lines[buffer->cursor_line].glyphs_cap) buffer->cursor_glyph = buffer->lines[buffer->cursor_line].glyphs_cap;
    if (buffer->cursor_glyph < 0) buffer->cursor_glyph = 0;
    */
    cursor_visible = TRUE;

    redraw_cursor_line(FALSE, TRUE);
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    switch(event->keyval) {
    case GDK_KEY_Up:
        move_cursor(-1, 0);
        return TRUE;
    case GDK_KEY_Down:
        move_cursor(1, 0);
        return TRUE;
    case GDK_KEY_Right:
        move_cursor(0, 1);
        return TRUE;
    case GDK_KEY_Left:
        move_cursor(0, -1);
        return TRUE;
    }

    if (gtk_im_context_filter_keypress(drarim, event)) {
        return TRUE;
    }
    
    printf("Unknown key sequence: %d\n", event->keyval);

    /* TODO:
       - manage pageup / pagedown
       - manage home/end
       - manage DELETE key
       - manage Enter key
       - manage Tab key
       - manage Backspace key
     */
    
    return TRUE;
}

static void text_entry_callback(GtkIMContext *context, gchar *str, gpointer data) {
    printf("entered: %s\n", str);

    /* TODO: reimplement
    buffer_line_insert_utf8_text(buffer, buffer->cursor_line, str, strlen(str), buffer->cursor_glyph, 1);
    */
    
    redraw_cursor_line(FALSE, TRUE);
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    double origin_x, origin_y;
    GtkAllocation allocation;

    redraw_cursor_line(FALSE, FALSE);

    gtk_widget_get_allocation(widget, &allocation);
    
    origin_y = calculate_y_origin(&allocation);
    origin_x = calculate_x_origin(&allocation);

    cursor_visible = TRUE;
    
    buffer_move_cursor_to_position(buffer, origin_x, origin_y, event->x, event->y);

    redraw_cursor_line(FALSE, FALSE);
    
    return TRUE;
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    double origin_y, origin_x, y;
    GtkAllocation allocation;
    display_line_t *line;

    gtk_widget_get_allocation(widget, &allocation);

    buffer_reflow_softwrap(buffer, allocation.width);

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

    for (line = buffer->display_line; line != NULL; line = line->next) {
        double line_end_width;
        
        if (y - buffer->line_height > event->area.y+event->area.height) break; /* if we passed the visible area the just stop displaying */
        if (y < event->area.y) { /* If the line is before the visible area, just increment y and move to the next line */
            y += buffer->line_height;
            continue;
        }

        line_end_width = buffer_line_adjust_glyphs(buffer, line, origin_x, y);
        cairo_show_glyphs(cr, line->real_line->glyphs + line->offset, line->size);

        if (line->offset != 0) {
            cairo_set_line_width(cr, 4.0);
            cairo_move_to(cr, 0.0, y-(buffer->ex_height/2.0));
            cairo_line_to(cr, buffer->left_margin, y-(buffer->ex_height/2.0));
            cairo_stroke(cr);
            cairo_set_line_width(cr, 2.0);
        }

        if (!line->hard_end) {
            cairo_set_line_width(cr, 4.0);
            cairo_move_to(cr, line_end_width, y-(buffer->ex_height/2.0));
            cairo_line_to(cr, allocation.width, y-(buffer->ex_height/2.0));
            cairo_stroke(cr);
            cairo_set_line_width(cr, 2.0);
        }
        
        y += buffer->line_height;
    }

    if (cursor_visible) {
        double cursor_x, cursor_y;

        buffer_cursor_position(buffer, origin_x, origin_y, &cursor_x, &cursor_y);

        /*cairo_set_source_rgb(cr, 119.0/255, 136.0/255, 153.0/255);*/
        cairo_rectangle(cr, cursor_x, cursor_y-buffer->ascent, 2, buffer->ascent+buffer->descent);
        cairo_fill(cr);
    }

    {
        char *posbox_text;
        asprintf(&posbox_text, " %d,%d %0.0f%%", buffer->cursor_line, buffer->cursor_glyph, (100.0 * buffer->cursor_line / buffer->display_lines_count));
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

    gtk_widget_set_can_focus(GTK_WIDGET(drar), TRUE);

    g_signal_connect(G_OBJECT(drar), "expose_event",
                     G_CALLBACK(expose_event_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "key-press-event",
                     G_CALLBACK(key_press_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "button-press-event",
                     G_CALLBACK(button_press_callback), NULL);

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

    g_timeout_add(500, (GSourceFunc)cursor_blinker, (gpointer)window);

    gtk_widget_show_all(window);

    gtk_widget_grab_focus(GTK_WIDGET(drar));
    gdk_window_set_events(gtk_widget_get_window(drar), gdk_window_get_events(gtk_widget_get_window(drar)) | GDK_BUTTON_PRESS_MASK);

    gtk_main();

    buffer_free(buffer);

    return 0;
}
