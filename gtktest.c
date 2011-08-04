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
gboolean cursor_visible = TRUE;

/* TODO:
   - cursor positioning
   - drawing modebox at bottom right (show line number, column number and percentage position)
   - editing
   - drawing header
*/

double calculate_x_origin(GtkAllocation *allocation) {
    double origin_x;

    if (allocation->width > buffer->rendered_width) {
        /* if there is enough space to show everything then there is no need to use the scrollbar */
        origin_x = buffer->left_margin;
    } else {
        origin_x = buffer->left_margin - gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment));
    }

    return origin_x;
}

double calculate_y_origin(GtkAllocation *allocation) {
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
}

static void move_cursor(int delta_line, int delta_char) {
    redraw_cursor_line(FALSE, FALSE);
    
    buffer->cursor_line += delta_line;

    if (buffer->cursor_line >= buffer->lines_cap) buffer->cursor_line = buffer->lines_cap-1;
    if (buffer->cursor_line < 0) buffer->cursor_line = 0;    

    buffer->cursor_glyph += delta_char;

    if (buffer->cursor_line >= buffer->lines_cap) buffer->cursor_glyph = 0; /* only happens when there are no lines */
    if (buffer->cursor_glyph > buffer->lines[buffer->cursor_line].glyphs_cap) buffer->cursor_glyph = buffer->lines[buffer->cursor_line].glyphs_cap;
    if (buffer->cursor_glyph < 0) buffer->cursor_glyph = 0;

    cursor_visible = TRUE;

    redraw_cursor_line(FALSE, TRUE);
}

static gboolean key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    switch(event->keyval) {
    case GDK_KEY_Up:
        move_cursor(-1, 0);
        break;
    case GDK_KEY_Down:
        move_cursor(1, 0);
        break;
    case GDK_KEY_Right:
        move_cursor(0, 1);
        break;
    case GDK_KEY_Left:
        move_cursor(0, -1);
        break;
    }


    return TRUE;
}

gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    int i;
    double origin_y, origin_x, y;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);

    printf("%dx%d +%dx%d (%dx%d)\n", event->area.x, event->area.y, event->area.width, event->area.height, allocation.width, allocation.height);

    cairo_set_source_rgb(cr, 0, 0, 1.0);
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_scaled_font(cr, buffer->cairofont);

    origin_y = calculate_y_origin(&allocation);
    origin_x = calculate_x_origin(&allocation);

    y = origin_y;

    /*printf("DRAWING!\n");*/

    for (i = 0; i < buffer->lines_cap; ++i) {
        if (y - buffer->line_height > event->area.y+event->area.height) break; /* if we passed the visible area the just stop displaying */
        if (y < event->area.y) { /* If the line is before the visible area, just increment y and move to the next line */
            y += buffer->line_height;
            continue;
        }

        buffer_line_adjust_glyphs(buffer, i, origin_x, y);
        cairo_show_glyphs(cr, buffer->lines[i].glyphs, buffer->lines[i].glyphs_cap);
        y += buffer->line_height;
    }

    if (cursor_visible) {
        double cursor_x, cursor_y;

        buffer_cursor_position(buffer, origin_x, origin_y, &cursor_x, &cursor_y);

        cairo_set_source_rgb(cr, 119.0/255, 136.0/255, 153.0/255);
        cairo_rectangle(cr, cursor_x, cursor_y-buffer->ascent, 2, buffer->ascent+buffer->descent);
        cairo_fill(cr);
    }
    

    gtk_adjustment_set_upper(GTK_ADJUSTMENT(adjustment), buffer->rendered_height);
    gtk_adjustment_set_page_size(GTK_ADJUSTMENT(adjustment), allocation.height);
    gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(adjustment), allocation.height/2);
    gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(adjustment), buffer->line_height);

    if (buffer->rendered_width < allocation.width) {
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
    cursor_visible = !cursor_visible;

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

    gtk_widget_set_can_focus(GTK_WIDGET(drar), TRUE);

    g_signal_connect(G_OBJECT(drar), "expose_event",
                     G_CALLBACK(expose_event_callback), NULL);

    g_signal_connect(G_OBJECT(drar), "key-press-event",
                     G_CALLBACK(key_press_callback), NULL);

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

    g_timeout_add(1000, (GSourceFunc)cursor_blinker, (gpointer)window);

    gtk_widget_show_all(window);

    gtk_widget_grab_focus(GTK_WIDGET(drar));

    gtk_main();

    buffer_free(buffer);

    return 0;
}
