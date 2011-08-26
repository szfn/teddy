#include "reshandle.h"

#include <stdlib.h>

static gboolean reshandle_expose_callback(GtkWidget *widget, GdkEventExpose *event, reshandle_t *reshandle) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);

    cairo_set_source_rgb(cr, 136.0/256, 136.0/256, 204.0/256);

    cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
    cairo_fill(cr);

    cairo_rectangle(cr, 2, 2, allocation.width - 4, allocation.height - 4);
    if (reshandle->modified) {
        cairo_set_source_rgb(cr, 0.0, 0.0, 153.0/256);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    }
    
    cairo_fill(cr);
    
    cairo_destroy(cr);
    //TODO: implement drawing

    return TRUE;
}

reshandle_t *reshandle_new(void) {
    reshandle_t *reshandle = malloc(sizeof(reshandle_t));
    reshandle->modified = 0;
    reshandle->resdr = gtk_drawing_area_new();
    gtk_widget_set_size_request(reshandle->resdr, 14, 14);
    g_signal_connect(G_OBJECT(reshandle->resdr), "expose_event", G_CALLBACK(reshandle_expose_callback), (gpointer)reshandle);
    /* TODO:
       - add button press/release events to implement editor/column resize
     */
    return reshandle;
}

void reshandle_free(reshandle_t *reshandle) {
    free(reshandle);
}
