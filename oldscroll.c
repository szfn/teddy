#include "oldscroll.h"

#include "global.h"

typedef struct _oldscroll_t {
	GtkDrawingArea a;
	GtkAdjustment *adj;
	bool drag;
	int autoscroll;
	double autoscroll_y;
} oldscroll_t;

typedef struct _oldscroll_class {
	GtkDrawingAreaClass parent_class;
} oldscroll_class;

static void gtk_oldscroll_class_init(oldscroll_class *klass);
static void gtk_oldscroll_init(oldscroll_t *os);

GType gtk_oldscroll_get_type(void) {
	static GType oldscroll_type = 0;

	if (!oldscroll_type) {
		static const GTypeInfo oldscroll_info = {
			sizeof(oldscroll_class),
			NULL,
			NULL,
			(GClassInitFunc)gtk_oldscroll_class_init,
			NULL,
			NULL,
			sizeof(oldscroll_t),
			0,
			(GInstanceInitFunc)gtk_oldscroll_init,
		};

		oldscroll_type = g_type_register_static(GTK_TYPE_DRAWING_AREA, "oldscroll_t", &oldscroll_info, 0);
	}

	return oldscroll_type;
}

static void gtk_oldscroll_class_init(oldscroll_class *class) {
	//nothing
}

static void gtk_oldscroll_init(oldscroll_t *os) {
}

static gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, oldscroll_t *os) {
	cairo_t *cr = gdk_cairo_create(widget->window);

	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);

	set_color_cfg(cr, config_intval(&global_config, CFG_SCROLLBAR_BG_COLOR));
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	double lower = gtk_adjustment_get_lower(os->adj),
		page = gtk_adjustment_get_page_size(os->adj),
		upper = gtk_adjustment_get_upper(os->adj) - page,
		value = gtk_adjustment_get_value(os->adj);

	double size = upper - lower;
	double adj_page = page / size * allocation.height;
	double adj_value = value / size * allocation.height;

	set_color_cfg(cr, config_intval(&global_config, CFG_EDITOR_BG_COLOR));
	cairo_rectangle(cr, 0, adj_value, allocation.width-1, adj_page);
	cairo_fill(cr);

	cairo_destroy(cr);

	return TRUE;
}

static gboolean scrolled_callback(GtkAdjustment *adj, oldscroll_t* os) {
	gtk_widget_queue_draw(GTK_WIDGET(os));
	return TRUE;
}

static gboolean map_callback(GtkWidget *widget, GdkEvent *event, oldscroll_t *os) {
	gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(os)), cursor_arrow);
	return FALSE;
}

static void scroll_by_page(oldscroll_t *os, double y, double direction) {
	GtkAllocation allocation;
	gtk_widget_get_allocation(GTK_WIDGET(os), &allocation);

	double page = gtk_adjustment_get_page_size(os->adj),
		value = gtk_adjustment_get_value(os->adj);

	value += direction * page * (y / allocation.height);

	gtk_adjustment_set_value(GTK_ADJUSTMENT(os->adj), value);
}

static void scroll_absolute(oldscroll_t *os, double y) {
	GtkAllocation allocation;
	gtk_widget_get_allocation(GTK_WIDGET(os), &allocation);

	double lower = gtk_adjustment_get_lower(os->adj),
		upper = gtk_adjustment_get_upper(os->adj),
		value = gtk_adjustment_get_value(os->adj),
		page = gtk_adjustment_get_page_size(os->adj);

	value = (upper - page - lower) * (y / allocation.height);

	gtk_adjustment_set_value(GTK_ADJUSTMENT(os->adj), value);
}

static gboolean scrollbar_autoscroll(oldscroll_t *os) {
	switch(os->autoscroll) {
	case -1:
		return FALSE;
	case 0:
		os->autoscroll = -1;
		return FALSE;
	case 1:
		scroll_by_page(os, os->autoscroll_y, -1.0);
		return TRUE;
	case 3:
		scroll_by_page(os, os->autoscroll_y, +1.0);
		return TRUE;
	}
	return FALSE;
}

static gboolean button_press_callback(GtkWidget *widget, GdkEventButton *event, oldscroll_t *os) {
	if (event->type == GDK_2BUTTON_PRESS) return TRUE;
	switch (event->button) {
	case 1:
		scroll_by_page(os, event->y, -1.0);
		os->autoscroll_y = event->y;
		if (os->autoscroll < 0) g_timeout_add(2*AUTOSCROLL_TIMO, (GSourceFunc)scrollbar_autoscroll, (gpointer)os);
		os->autoscroll = 1;
		break;
	case 2:
		scroll_absolute(os, event->y);
		os->drag = true;
		break;
	case 3:
		scroll_by_page(os, event->y, +1.0);
		os->autoscroll_y = event->y;
		if (os->autoscroll < 0) g_timeout_add(2*AUTOSCROLL_TIMO, (GSourceFunc)scrollbar_autoscroll, (gpointer)os);
		os->autoscroll = 3;
		break;
	}

	return TRUE;
}

static gboolean motion_callback(GtkWidget *widget, GdkEventMotion *event, oldscroll_t *os) {
	if (os->autoscroll) os->autoscroll_y = event->y;
	if (!os->drag) return TRUE;
	scroll_absolute(os, event->y);
	return TRUE;
}

static gboolean button_release_callback(GtkWidget *widget, GdkEventButton *event, oldscroll_t *os) {
	os->drag = false;
	if (event->button != 2) os->autoscroll = 0;
	return TRUE;
}

GtkWidget *oldscroll_new(GtkAdjustment *adjustment) {
	GtkWidget *oldscroll_widget = g_object_new(GTK_TYPE_OLDSCROLL, NULL);
	oldscroll_t *r = GTK_OLDSCROLL(oldscroll_widget);

	r->adj = adjustment;
	r->drag = false;
	r->autoscroll = -1;
	g_signal_connect(G_OBJECT(r), "expose_event", G_CALLBACK(expose_event_callback), r);

	gtk_widget_set_size_request(oldscroll_widget, RESHANDLE_SIZE, -1);

	gtk_widget_add_events(oldscroll_widget, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);

	g_signal_connect(G_OBJECT(r), "map_event", G_CALLBACK(map_callback), r);
	g_signal_connect(G_OBJECT(r->adj), "value_changed", G_CALLBACK(scrolled_callback), (gpointer)r);
	g_signal_connect(G_OBJECT(r->adj), "changed", G_CALLBACK(scrolled_callback), (gpointer)r);

	g_signal_connect(G_OBJECT(oldscroll_widget), "button-press-event", G_CALLBACK(button_press_callback), r);
	g_signal_connect(G_OBJECT(oldscroll_widget), "button-release-event", G_CALLBACK(button_release_callback), r);
	g_signal_connect(G_OBJECT(oldscroll_widget), "motion-notify-event", G_CALLBACK(motion_callback), r);

	return oldscroll_widget;
}
