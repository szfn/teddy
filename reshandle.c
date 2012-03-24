#include "reshandle.h"

#include <stdlib.h>

#include "column.h"
#include "columns.h"
#include "editor.h"
#include "buffers.h"

#include "global.h"

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
	int shift = event->state & GDK_SHIFT_MASK;

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
		reshandle->origin_x = event->x;
		reshandle->origin_y = event->y;
		return TRUE;
	}

	if ((event->type == GDK_2BUTTON_PRESS) && (event->button == 1)) {
		if (column_remove_others(reshandle->editor->column, reshandle->editor) == 0) {
			columns_remove_others(columnset, reshandle->editor->column, reshandle->editor);
		}
		return TRUE;
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 2)) {
		editor_close_editor(reshandle->editor);
		return TRUE;
	}

	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
		if (shift) {
			editor_t *new_editor = columns_new_after(columnset, reshandle->editor->column, null_buffer());
			if (new_editor != NULL) editor_grab_focus(new_editor, true);
		} else {
			editor_t *new_editor = column_new_editor(reshandle->editor->column, null_buffer());
			if (new_editor == NULL) {
				heuristic_new_frame(columnset, reshandle->editor, null_buffer());
			} else {
				editor_grab_focus(new_editor, true);
			}
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean reshandle_button_release_callback(GtkWidget *widget, GdkEventButton *event, reshandle_t *reshandle) {
	editor_t *prev_editor;
	column_t *prev_column;

	if (event->button != 1) return TRUE;

	double changey = event->y - reshandle->origin_y;
	double changex = event->x - reshandle->origin_x;

	//printf("Resizing end\n");

	prev_editor = column_get_editor_before(reshandle->column, reshandle->editor);
	if (prev_editor != NULL) {
		GtkAllocation allocation;

		gtk_widget_get_allocation(reshandle->editor->container, &allocation);
		double cur_height = allocation.height - changey;

		gtk_widget_get_allocation(prev_editor->container, &allocation);
		double prev_height = allocation.height + changey;

		column_resize_editor_pair(prev_editor, prev_height, reshandle->editor, cur_height);
	}

	prev_column = columns_get_column_before(columnset, reshandle->column);
	if (prev_column != NULL) {
		GtkAllocation allocation;

		gtk_widget_get_allocation(GTK_WIDGET(columnset), &allocation);
		double change_fraction = changex / allocation.width;

		prev_column->fraction = prev_column->fraction + change_fraction;
		reshandle->column->fraction -= change_fraction;

		columns_reallocate(columnset);
		gtk_widget_queue_draw(GTK_WIDGET(columnset));
	}

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
