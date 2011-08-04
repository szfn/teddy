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

#include "buffer.h"

FT_Library library;


buffer_t *buffer;

GtkObject *adjustment, *hadjustment;
GtkWidget *drar;
GtkWidget *drarhscroll;

/* TODO:
   - cursor positioning
   - editing
   - drawing header
*/

gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    int i;
    double y, x;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);

    cairo_set_source_rgb(cr, 0, 0, 255);
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_set_scaled_font(cr, buffer->cairofont);

    if (allocation.height > buffer->rendered_height) {
        /* if there is enough space to show everything then there is no need to use the scrollbar */
        y = buffer->line_height;
    } else {
        y = buffer->line_height - gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment));
    }

    if (allocation.width > buffer->rendered_width) {
        /* if there is enough space to show everything then there is no need to use the scrollbar */
        x = buffer->left_margin;
    } else {
        x = buffer->left_margin - gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment));
    }

    /*printf("DRAWING!\n");*/

    for (i = 0; i < buffer->lines_cap; ++i) {
        if (y - buffer->line_height > allocation.height) break; /* if we passed the visible area the just stop displaying */
        if (y < 0) { /* If the line is before the visible area, just increment y and move to the next line */
            y += buffer->line_height;
            continue;
        }

        buffer_line_adjust_glyphs(buffer, i, x, y);
        cairo_show_glyphs(cr, buffer->lines[i].glyphs, buffer->lines[i].glyphs_cap);
        y += buffer->line_height;
    }

    gtk_adjustment_set_upper(GTK_ADJUSTMENT(adjustment), buffer->rendered_height);
    gtk_adjustment_set_page_size(GTK_ADJUSTMENT(adjustment), allocation.height);
    gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(adjustment), allocation.height/2);
    gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(adjustment), buffer->line_height);

    if (buffer->rendered_width < allocation.width) {
        gtk_widget_hide(GTK_WIDGET(drarhscroll));
    } else {
        gtk_widget_show(GTK_WIDGET(drarhscroll));
        gtk_adjustment_set_upper(GTK_ADJUSTMENT(hadjustment), buffer->rendered_width);
        gtk_adjustment_set_page_size(GTK_ADJUSTMENT(hadjustment), allocation.width);
        gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(hadjustment), allocation.width/2);
        gtk_adjustment_set_step_increment(GTK_ADJUSTMENT(hadjustment), buffer->em_advance);
    }

    cairo_destroy(cr);
  
    return TRUE;
}

gboolean scrolled_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    /*printf("Scrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)));*/
    gtk_widget_queue_draw(drar);
    return TRUE;
}

gboolean hscrolled_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    /* printf("HScrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(hadjustment)));*/
    gtk_widget_queue_draw(drar);
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

    g_signal_connect(G_OBJECT(drar), "expose_event",
                     G_CALLBACK(expose_event_callback), NULL);

    {
        GtkWidget *drarscroll = gtk_vscrollbar_new((GtkAdjustment *)(adjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        drarhscroll = gtk_hscrollbar_new((GtkAdjustment *)(hadjustment = gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0)));
        GtkWidget *table = gtk_table_new(2, 2, FALSE);

        gtk_table_attach(GTK_TABLE(table), drar, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 1, 1);
        gtk_table_attach(GTK_TABLE(table), drarscroll, 1, 2, 0, 1, 0, GTK_EXPAND|GTK_FILL, 1, 1);
        gtk_table_attach(GTK_TABLE(table), drarhscroll, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 1, 1);

        gtk_container_add(GTK_CONTAINER(window), table);

        g_signal_connect_swapped(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), NULL);
        g_signal_connect_swapped(G_OBJECT(drarhscroll), "value_changed", G_CALLBACK(hscrolled_callback), NULL);
    }

    gtk_widget_show_all(window);

    gtk_main();

    buffer_free(buffer);

    return 0;
}
