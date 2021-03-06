#include "column.h"

#include "global.h"

#define COLUMN_MINIMUM_WIDTH 50

typedef struct _column_t {
	GtkBox box;

	double fraction;
	bool maximized;
	double saved_fractions[512];
} column_t;

typedef struct _column_class {
	GtkBoxClass parent_class;
} column_class;

static void gtk_column_class_init(column_class *klass);
static void gtk_column_init(column_t *column);
static void gtk_column_size_request(GtkWidget *widget, GtkRequisition *requisition);

GType gtk_column_get_type(void) {
	static GType column_type = 0;

	if (!column_type) {
		static const GTypeInfo column_info = {
			sizeof(column_class),
			NULL,
			NULL,
			(GClassInitFunc)gtk_column_class_init,
			NULL,
			NULL,
			sizeof(column_t),
			0,
			(GInstanceInitFunc)gtk_column_init,
		};

		column_type = g_type_register_static(GTK_TYPE_BOX, "column_t", &column_info, 0);
	}

	return column_type;
}

static void gtk_column_class_init(column_class *class) {
	GtkWidgetClass *widget_class = (GtkWidgetClass *)class;

	widget_class->size_request = gtk_column_size_request;
	widget_class->size_allocate = gtk_column_size_allocate;
}

static void gtk_column_init(column_t *column) {
}

column_t *column_new(gint spacing) {
	GtkWidget *column_widget = g_object_new(GTK_TYPE_COLUMN, NULL);
	alloc_assert(column_widget);
	column_t *column = GTK_COLUMN(column_widget);

	GTK_BOX(column)->spacing = spacing;
	GTK_BOX(column)->homogeneous = FALSE;

	column->fraction = 1.0;
	column->maximized = false;

	return column;
}

static void gtk_column_size_request(GtkWidget *widget, GtkRequisition *requisition) {
	GtkBox *box = GTK_BOX(widget);

	requisition->width = COLUMN_MINIMUM_WIDTH;
	requisition->height = GTK_CONTAINER(box)->border_width * 2;

	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		GtkWidget *child_frame = child->widget;
		children = children->next;

		GtkRequisition child_requisition;
		gtk_widget_size_request(child_frame, &child_requisition);

		if (child_requisition.width > requisition->width) requisition->width = child_requisition.width;

		requisition->height += child_requisition.height + (child->padding * 2);
	}

	requisition->width += GTK_CONTAINER(box)->border_width * 2;
}

void gtk_column_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
	GtkBox *box = GTK_BOX(widget);

	widget->allocation = *allocation;

	double total_height_request = 0.0;
	gint minimum_residual_height = 0;
	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		GtkWidget *child_frame = child->widget;
		children = children->next;

		total_height_request += tframe_fraction(GTK_TFRAME(child_frame));

		GtkRequisition child_requisition;
		gtk_widget_size_request(child_frame, &child_requisition);
		minimum_residual_height += child_requisition.height;
	}

	if (total_height_request <= 0.1) total_height_request = 0.1;

	gint y = allocation->y + GTK_CONTAINER(box)->border_width;
	gint remaining_height = allocation->height;

	GtkWidget *child_frame = NULL;
	GtkAllocation child_allocation;

	children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		child_frame = child->widget;
		children = children->next;

		double fraction = tframe_fraction(GTK_TFRAME(child_frame));

		GtkRequisition child_requisition;
		gtk_widget_size_request(child_frame, &child_requisition);

		child_allocation.x = allocation->x + GTK_CONTAINER(box)->border_width;
		child_allocation.width = MAX(1, (gint)allocation->width - (gint)GTK_CONTAINER(box)->border_width * 2);
		child_allocation.y = y;

		gint desired_height = allocation->height * (fraction/total_height_request);
		minimum_residual_height -= child_requisition.height;

		if (desired_height < child_requisition.height) {
			desired_height = child_requisition.height;
		}

		if (remaining_height - desired_height < minimum_residual_height) {
			desired_height = remaining_height - minimum_residual_height;
		}

		if (desired_height > remaining_height) {
			desired_height = remaining_height;
		}

		child_allocation.height = desired_height;

		remaining_height -= desired_height;

		y += child_allocation.height;

		gtk_widget_size_allocate(child_frame, &child_allocation);
	}

	if (child_frame != NULL) {
		child_allocation.height += remaining_height;
		gtk_widget_size_allocate(child_frame, &child_allocation);
	}
}

void column_add_after(column_t *column, tframe_t *before_tf, tframe_t *tf, bool set_fraction) {
	gtk_box_pack_start(GTK_BOX(column), GTK_WIDGET(tf), TRUE, TRUE, 1);

	int before_index = -1;

	if (before_tf != NULL) {
		GList *list = gtk_container_get_children(GTK_CONTAINER(column));
		before_index = 0;
		for (GList *cur = list; cur != NULL; cur = cur->next, ++before_index) {
			if (before_tf == cur->data) break;
		}
		g_list_free(list);

		if (set_fraction) {
			tframe_fraction_set(before_tf, tframe_fraction(before_tf) / 2);
			tframe_fraction_set(tf, tframe_fraction(before_tf));
		}
	} else {
		if (set_fraction) tframe_fraction_set(tf, 10.0);
	}

	gtk_box_reorder_child(GTK_BOX(column), GTK_WIDGET(tf), before_index+1);

	gtk_widget_show_all(GTK_WIDGET(tf));
	gtk_widget_queue_draw(GTK_WIDGET(column));

	column->maximized = false;
}

void column_append(column_t *column, tframe_t *tf, bool set_fraction) {
	GList *list_frames = gtk_container_get_children(GTK_CONTAINER(column));
	GList *last = g_list_last(list_frames);
	column_add_after(column, (last != NULL) ? last->data : NULL, tf, set_fraction);
	g_list_free(list_frames);
}

double column_fraction(column_t *column) {
	return column->fraction;
}

void column_fraction_set(column_t *column, double fraction) {
	column->fraction = fraction;
}

bool column_find_frame(column_t *column, tframe_t *tf, tframe_t **before_tf, tframe_t **after_tf) {
	if (before_tf != NULL) {
		*before_tf = NULL;
	}

	if (after_tf != NULL) {
		*after_tf = NULL;
	}

	GList *list = gtk_container_get_children(GTK_CONTAINER(column));

	GList *prev = NULL, *cur = NULL;
	for (cur = list; cur != NULL; prev = cur, cur = cur->next) {
		if (cur->data == tf) break;
	}

	bool r = (cur != NULL);

	if (cur != NULL) {
		if (before_tf != NULL) {
			*before_tf = (prev != NULL) ? GTK_TFRAME(prev->data) : NULL;
		}

		if (after_tf != NULL) {
			*after_tf = (cur->next != NULL) ? GTK_TFRAME(cur->next->data) : NULL;
		}
	}

	g_list_free(list);

	return r;
}

int column_frame_number(column_t *column) {
	return g_list_length(GTK_BOX(column)->children);
}

bool column_remove(column_t *column, tframe_t *frame, bool reparenting, bool resist) {
	tframe_t *before_tf, *after_tf;

	if (!reparenting)
		if (!tframe_close(frame, resist)) return false;

	if (column_find_frame(column, frame, &before_tf, &after_tf)) {
		if (before_tf != NULL) {
			tframe_fraction_set(before_tf, tframe_fraction(frame) + tframe_fraction(before_tf));
		} else if (after_tf != NULL) {
			tframe_fraction_set(after_tf, tframe_fraction(frame) + tframe_fraction(after_tf));
		}
	}

	gtk_container_remove(GTK_CONTAINER(column), GTK_WIDGET(frame));

	column->maximized = false;

	return true;
}

int column_hide_or_restore_others(column_t *column, tframe_t *frame) {
	if (column->maximized) {
		column_restore_others(column);
		return 0;
	} else {
		return column_hide_others(column, frame);
	}
}

int column_hide_others(column_t *column, tframe_t *frame) {
	double fraction = tframe_fraction(frame);
	int c = 0, i = 0;
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		double f = tframe_fraction(GTK_TFRAME(cur->data));
		if (!column->maximized) {
			column->saved_fractions[i] = f;
			++i;
		}
		if (cur->data != frame) {
			fraction += f;
			tframe_fraction_set(GTK_TFRAME(cur->data), 0);
			++c;
		}
	}
	g_list_free(list);

	column->maximized = true;

	tframe_fraction_set(frame, fraction);

	gtk_column_size_allocate(GTK_WIDGET(column), &(GTK_WIDGET(column)->allocation));

	if (GTK_IS_TEDITOR(tframe_content(frame))) {
		GTK_TEDITOR(tframe_content(frame))->center_on_cursor_after_next_expose = true;
	}

	return c;
}

void column_restore_others(column_t *column) {
	if (!column->maximized) return;
	column->maximized = false;

	int i = 0;
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		tframe_fraction_set(GTK_TFRAME(cur->data), column->saved_fractions[i]);
		++i;
	}
	g_list_free(list);

	gtk_column_size_allocate(GTK_WIDGET(column), &(GTK_WIDGET(column)->allocation));
}

tframe_t *column_get_frame_from_position(column_t *column, double x, double y, bool *ontag) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(cur->data, &allocation);
		if (inside_allocation(x, y, &allocation)) {
			GtkWidget *content = tframe_content(GTK_TFRAME(cur->data));
			GtkAllocation drar_allocation;
			gtk_widget_get_allocation(content, &drar_allocation);
			allocation.y = drar_allocation.y;
			*ontag = !inside_allocation(x, y, &allocation);
			GtkWidget *r = cur->data;
			g_list_free(list);
			return GTK_TFRAME(r);
		}

	}
	g_list_free(list);
	return NULL;
}

bool column_close(column_t *column) {
	int n = column_frame_number(column);

	int c = 0;
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		if (tframe_close(GTK_TFRAME(cur->data), false)) {
			column_remove(column, GTK_TFRAME(cur->data), false, false);
			++c;
		}
	}
	g_list_free(list);

	return (n == c);
}

void column_expand_frame(column_t *column, tframe_t *frame) {
	if (tframe_fraction(frame) > 0.2) return;

	column->maximized = false;

	tframe_t *biggest = NULL;
	double biggest_fraction = 0.0;

	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		tframe_t *curtf = GTK_TFRAME(cur->data);
		if (tframe_fraction(curtf) > biggest_fraction) {
			biggest_fraction = tframe_fraction(curtf);
			biggest = curtf;
		}
	}
	g_list_free(list);

	if (biggest == NULL) return;

	double fraction = 2.0;
	if (fraction > biggest_fraction) fraction = biggest_fraction / 2.0;

	tframe_fraction_set(biggest, biggest_fraction - fraction);
	tframe_fraction_set(frame, fraction);
	gtk_column_size_allocate(GTK_WIDGET(column), &(GTK_WIDGET(column)->allocation));
}
