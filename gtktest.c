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

GtkObject *adjustment;
GtkWidget *drar;

/* TODO:
   - horizontal scrollbar
   - cursor positioning
   - editing
   - drawing header
*/

gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    int i;
    double y ;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);

    cairo_set_source_rgb(cr, 0, 0, 255);
    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_set_scaled_font(cr, buffer->cairofont);

    y = buffer->line_height - gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment));

    /*printf("DRAWING!\n");*/

    for (i = 0; i < buffer->lines_cap; ++i) {
        double x = 5.0; /* left margin */

        if (y - buffer->line_height > allocation.height) break; /* if we passed the visible area the just stop displaying */
        if (y < 0) { /* If the line is before the visible area, just increment y and move to the next line */
            y += buffer->line_height;
            continue;
        }

        buffer_line_adjust_glyphs(buffer, i, x, y);
        cairo_show_glyphs(cr, buffer->lines[i].glyphs, buffer->lines[i].glyphs_cap);
        y += buffer->line_height;
    }

    gtk_adjustment_set_upper(GTK_ADJUSTMENT(adjustment), (buffer->lines_cap+1) * buffer->line_height);
    gtk_adjustment_set_page_size(GTK_ADJUSTMENT(adjustment), allocation.height);
    gtk_adjustment_set_page_increment(GTK_ADJUSTMENT(adjustment), allocation.height/2);

    cairo_destroy(cr);
  
    return TRUE;
}

gboolean scrolled_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    printf("Scrolled to: %g\n", gtk_adjustment_get_value(GTK_ADJUSTMENT(adjustment)));
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
        GtkWidget *hbox = gtk_hbox_new(FALSE, 1);

        gtk_container_add(GTK_CONTAINER(hbox), drar);
        gtk_container_add(GTK_CONTAINER(hbox), drarscroll);

        gtk_box_set_child_packing(GTK_BOX(hbox), drar, TRUE, TRUE, 0, GTK_PACK_START);
        gtk_box_set_child_packing(GTK_BOX(hbox), drarscroll, FALSE, FALSE, 0, GTK_PACK_END);

        gtk_container_add(GTK_CONTAINER(window), hbox);

        g_signal_connect_swapped(G_OBJECT(drarscroll), "value_changed", G_CALLBACK(scrolled_callback), NULL);
    }

    gtk_widget_show_all(window);

    gtk_main();

    buffer_free(buffer);

    return 0;
}
