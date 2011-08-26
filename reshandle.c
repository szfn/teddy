#include "reshandle.h"

#include <stdlib.h>

#include "column.h"

static gboolean reshandle_expose_callback(GtkWidget *widget, GdkEventExpose *event, reshandle_t *reshandle) {
    cairo_t *cr = gdk_cairo_create(widget->window);
    GtkAllocation allocation;

    gdk_window_set_cursor(gtk_widget_get_window(widget), gdk_cursor_new(GDK_SIZING));

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

    return TRUE;
}

static gboolean reshandle_button_press_callback(GtkWidget *widget, GdkEventButton *event, reshandle_t *reshandle) {
    printf("Starting resize\n");
    reshandle->origin_x = event->x;
    reshandle->origin_y = event->y;
    return TRUE;
}

static gboolean reshandle_button_release_callback(GtkWidget *widget, GdkEventButton *event, reshandle_t *reshandle) {
    editor_t *prev_editor;

    double changey = event->y - reshandle->origin_y;
    //double changex = event->x - reshandle->origin_x;

    printf("Resizing end\n");

    prev_editor = column_get_editor_before(reshandle->column, reshandle->editor);
    if (prev_editor != NULL) {
        GtkAllocation allocation;
        
        reshandle->editor->allocated_vertical_space -= changey;
        if (reshandle->editor->allocated_vertical_space < 50) reshandle->editor->allocated_vertical_space = 50;

        gtk_widget_get_allocation(prev_editor->table, &allocation);

        if (allocation.height > prev_editor->allocated_vertical_space) {
            double new_height = allocation.height += changey;
            if (new_height < prev_editor->allocated_vertical_space) {
                prev_editor->allocated_vertical_space = new_height;
            }
        } else {
            prev_editor->allocated_vertical_space += changey;
        }
        
        column_adjust_size(reshandle->column);
        gtk_widget_queue_draw(reshandle->column->editors_vbox);
    }
    
    //TODO: resize columns
    return TRUE;
}

reshandle_t *reshandle_new(column_t *column, editor_t *editor) {
    reshandle_t *reshandle = malloc(sizeof(reshandle_t));

    reshandle->modified = 0;
    reshandle->column = column;
    reshandle->editor = editor;
    
    reshandle->resdr = gtk_drawing_area_new();
    gtk_widget_set_size_request(reshandle->resdr, 14, 14);

    gtk_widget_add_events(reshandle->resdr, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

    g_signal_connect(G_OBJECT(reshandle->resdr), "expose_event", G_CALLBACK(reshandle_expose_callback), (gpointer)reshandle);
    g_signal_connect(G_OBJECT(reshandle->resdr), "button_press_event", G_CALLBACK(reshandle_button_press_callback), (gpointer)reshandle);
    g_signal_connect(G_OBJECT(reshandle->resdr), "button_release_event", G_CALLBACK(reshandle_button_release_callback), (gpointer)reshandle);
    
    return reshandle;
}

void reshandle_free(reshandle_t *reshandle) {
    free(reshandle);
}
