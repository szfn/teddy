#include "columns.h"

#include "buffers.h"
#include "global.h"

typedef struct _columns_t {
	GtkBox box;
	column_t *active_column;
} columns_t;

typedef struct _columns_class {
	GtkBoxClass parent_class;
} columns_class;

static void gtk_columns_class_init(columns_class *klass);
static void gtk_columns_init(columns_t *columns);
static void gtk_columns_size_request(GtkWidget *widget, GtkRequisition *requisition);

GType gtk_columns_get_type(void) {
	static GType columns_type = 0;

	if (!columns_type) {
		static const GTypeInfo columns_info = {
			sizeof(columns_class),
			NULL,
			NULL,
			(GClassInitFunc)gtk_columns_class_init,
			NULL,
			NULL,
			sizeof(columns_t),
			0,
			(GInstanceInitFunc)gtk_columns_init,
		};

		columns_type = g_type_register_static(GTK_TYPE_BOX, "columns_t", &columns_info, 0);
	}

	return columns_type;
}

static void gtk_columns_class_init(columns_class *class) {
	GtkWidgetClass *widget_class = (GtkWidgetClass *)class;

	widget_class->size_request = gtk_columns_size_request;
	widget_class->size_allocate = gtk_columns_size_allocate;
}

static void gtk_columns_init(columns_t *columns) {
}

columns_t *columns_new(void) {
	GtkWidget *columns_widget = g_object_new(GTK_TYPE_COLUMNS, NULL);
	alloc_assert(columns_widget);
	columns_t *columns = GTK_COLUMNS(columns_widget);

	GTK_BOX(columns)->spacing = 0;
	GTK_BOX(columns)->homogeneous = FALSE;

	columns->active_column = NULL;

	return columns;
}

static void gtk_columns_size_request(GtkWidget *widget, GtkRequisition *requisition) {
	GtkBox *box = GTK_BOX(widget);

	requisition->width = 1;
	requisition->height = 1;

	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		GtkWidget *col = child->widget;
		children = children->next;

		GtkRequisition child_requisition;
		gtk_widget_size_request(col, &child_requisition);

		if (child_requisition.height > requisition->height) requisition->height = child_requisition.height;

		requisition->width += child_requisition.width;
	}

	requisition->width += GTK_CONTAINER(box)->border_width * 2;
	requisition->height += GTK_CONTAINER(box)->border_width * 2;
}

void gtk_columns_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
	GtkBox *box = GTK_BOX(widget);

	widget->allocation = *allocation;

	//printf("Column fractions/width requisition: ");

	double total_width_request = 0.0;
	gint minimum_residual_width = 0;
	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		GtkWidget *col = child->widget;
		children = children->next;

		total_width_request += column_fraction(GTK_COLUMN(col));

		GtkRequisition child_requisition;
		gtk_widget_size_request(col, &child_requisition);

		//printf("%g/%d ", column_fraction(GTK_COLUMN(col)), child_requisition.width);

		minimum_residual_width += child_requisition.width;
	}

	//printf("\n");

	gint x = allocation->x + GTK_CONTAINER(box)->border_width;
	gint remaining_width = allocation->width;

	//printf("Columns size allocate: ");

	GtkWidget *col = NULL;
	GtkAllocation child_allocation;

	children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		col = child->widget;
		children = children->next;

		GtkRequisition child_requisition;
		gtk_widget_size_request(col, &child_requisition);

		child_allocation.y = allocation->y + GTK_CONTAINER(box)->border_width;
		child_allocation.x = x;
		child_allocation.height = MAX(1, (gint)allocation->height - (gint)GTK_CONTAINER(box)->border_width * 2);

		gint desired_width = allocation->width * (column_fraction(GTK_COLUMN(col)) /  total_width_request);
		minimum_residual_width -= child_requisition.width;

		if (desired_width < child_requisition.width) {
			desired_width = child_requisition.width;
		}

		if (remaining_width - desired_width < minimum_residual_width) {
			desired_width = remaining_width - minimum_residual_width;
		}

		if (desired_width > remaining_width) {
			desired_width = remaining_width;
		}

		child_allocation.width = desired_width;

		remaining_width -= desired_width;

		x += child_allocation.width;

		//printf("%d ", child_allocation.width);

		gtk_widget_size_allocate(col, &child_allocation);
	}

	if (col != NULL) {
		child_allocation.width += remaining_width;
		gtk_widget_size_allocate(col, &child_allocation);
	}

	//printf("\n");
}

void columns_add_after(columns_t *columns, column_t *before_col, column_t *col, bool set_fraction) {
	gtk_box_pack_start(GTK_BOX(columns), GTK_WIDGET(col), TRUE, TRUE, 1);

	int before_index = -1;

	if (before_col != NULL) {
		GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
		before_index = 0;
		for (GList *cur = list; cur != NULL; cur = cur->next, ++before_index) {
			if (before_col == cur->data) break;
		}
		g_list_free(list);

		if (set_fraction) {
			double f = column_fraction(before_col);
			column_fraction_set(before_col,  f * 0.6);
			column_fraction_set(col, f * 0.4);
		}
	} else {
		if (set_fraction) column_fraction_set(col, 10.0);
	}

	gtk_box_reorder_child(GTK_BOX(columns), GTK_WIDGET(col), before_index+1);

	gtk_widget_show_all(GTK_WIDGET(col));
	gtk_widget_queue_draw(GTK_WIDGET(columns));
}

void columns_append(columns_t *columns, column_t *column, bool set_fraction) {
	GList *list_cols = gtk_container_get_children(GTK_CONTAINER(columns));
	GList *last = g_list_last(list_cols);
	columns_add_after(columns, (last != NULL) ? last->data : NULL, column, set_fraction);
	g_list_free(list_cols);
}

bool columns_find_frame(columns_t *columns, tframe_t *tf, column_t **before_col, column_t **frame_col, column_t **after_col, tframe_t **before_tf, tframe_t **after_tf) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));

	GList *prev = NULL, *cur = NULL;
	for (cur = list; cur != NULL; prev = cur, cur = cur->next) {
		if (column_find_frame(GTK_COLUMN(cur->data), tf, before_tf, after_tf)) {
			break;
		}
	}

	bool r = (cur != NULL);

	if (cur != NULL) {
		if (before_col != NULL) {
			*before_col = (prev != NULL) ? GTK_COLUMN(prev->data) : NULL;
		}

		if (after_col != NULL) {
			*after_col = (cur->next != NULL) ? GTK_COLUMN(cur->next->data) : NULL;
		}

		if (frame_col != NULL) {
			*frame_col = GTK_COLUMN(cur->data);
		}
	}

	g_list_free(list);

	return r;
}

static column_t *columns_get_column_from_position(columns_t *columns, double x, double y) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(cur->data, &allocation);
		//printf("Comparing (%g,%g) with (%d,%d) (%d,%d)\n", x, y, allocation.x, allocation.y, allocation.x+allocation.width, allocation.y+allocation.height);
		if ((x >= allocation.x)
			&& (x <= allocation.x + allocation.width)
			&& (y >= allocation.y)
			&& (y <= allocation.y + allocation.height)) {
			GtkWidget *r = cur->data;
			g_list_free(list);
			return GTK_COLUMN(r);
		}
	}
	g_list_free(list);
	return NULL;
}

tframe_t *columns_get_frame_from_position(columns_t *columns, double x, double y, bool *ontag) {
	column_t *column = columns_get_column_from_position(columns, x, y);
	if (column == NULL) {
		return NULL;
	}
	return column_get_frame_from_position(column, x, y, ontag);
}

column_t *columns_active(columns_t *columns) {
	return columns->active_column;
}

void columns_set_active(columns_t *columns, column_t *column) {
	columns->active_column = column;
}

int columns_column_number(columns_t *columns) {
	return g_list_length(GTK_BOX(columns)->children);
}

void columns_find_column(columns_t *columns, column_t *column, column_t **before_col, column_t **after_col) {
	if (before_col != NULL) *before_col = NULL;
	if (after_col != NULL) *after_col = NULL;

	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	GList *prev = NULL, *cur;
	for (cur = list; cur != NULL; prev = cur, cur = cur->next) {
		if (cur->data == column) break;
	}

	if (cur != NULL) {
		if (before_col != NULL) {
			*before_col = (prev != NULL) ? GTK_COLUMN(prev->data) : NULL;
		}

		if (after_col != NULL) {
			*after_col = (cur->next != NULL) ? GTK_COLUMN(cur->next->data) : NULL;
		}
	}

	g_list_free(list);
}

bool columns_remove(columns_t *columns, column_t *column) {
	if (columns_column_number(columns) == 1) {
		quick_message("Error", "Can not remove last column of the window");
		return false;
	}

	column_t *before_col, *after_col;
	columns_find_column(columns, column, &before_col, &after_col);

	if (!column_close(column)) return false;

	if (before_col != NULL) {
		column_fraction_set(before_col, column_fraction(column) + column_fraction(before_col));
	} else if (after_col != NULL) {
		column_fraction_set(after_col, column_fraction(column) + column_fraction(after_col));
	}

	gtk_container_remove(GTK_CONTAINER(columns), GTK_WIDGET(column));

	return true;
}

int columns_remove_others(columns_t *columns, column_t *column) {
	int c = 0;
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		if (cur->data != column) {
			if (columns_remove(columns, GTK_COLUMN(cur->data)))
				++c;
		}
	}
	g_list_free(list);
	return c;
}

tframe_t *new_in_column(columns_t *columns, column_t *column, buffer_t *buffer) {
	GtkWidget *content;
	char *title = "";

	if (buffer != NULL) {
		content = GTK_WIDGET(new_editor(buffer, false));
		title = buffer->path;
	} else {
		content = gtk_label_new("");
	}

	tframe_t *frame = tframe_new(title, content, columns);

	if (column_frame_number(column) == 0) {
		column_add_after(column, NULL, frame, true);
		return frame;
	}

	double epsilon = 16.0;
	if (buffer != NULL) {
		epsilon = buffer->line_height;
	}

	GList *list = gtk_container_get_children(GTK_CONTAINER(column));

	GList *biggest_children = NULL;
	double biggest_children_height = 0.0;

	for (GList *cur = list; cur != NULL; cur = cur->next) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(cur->data, &allocation);

		if (allocation.height >= biggest_children_height - epsilon) {
			biggest_children = cur;
			biggest_children_height = allocation.height;
		}
	}

	column_add_after(column, GTK_TFRAME(biggest_children->data), frame, true);

	g_list_free(list);

	return frame;
}

tframe_t *heuristic_new_frame(columns_t *columns, tframe_t *spawning_frame, buffer_t *buffer) {
	tframe_t *r = NULL;

	//printf("heuristic_new_frame\n");
	GList *list_cols = gtk_container_get_children(GTK_CONTAINER(columns));

	column_t *spawning_col = NULL;
	if (spawning_frame != NULL) {
		columns_find_frame(columns, spawning_frame, NULL, &spawning_col, NULL, NULL, NULL);
	}

	if (columns_active(columns) == NULL)
		columns_set_active(columns, spawning_col);

	if (columns_column_number(columns) == 0) {
		column_t *col = column_new(0);
		columns_add_after(columns, NULL, col, true);
		r = new_in_column(columns, col, buffer);
		goto heuristic_new_frame_return;
	}

	{ // search for a column without anything inside
		//printf("\tsearch for empty columns\n");
		for (GList *cur = list_cols; cur != NULL; cur = cur->next) {
			if (!GTK_IS_COLUMN(cur->data)) continue;
			column_t *col = GTK_COLUMN(cur->data);

			if (column_frame_number(col) <= 0) {
				r = new_in_column(columns, col, buffer);
				goto heuristic_new_frame_return;
			}
		}
	}

	// if there is only one column and it's large split it (otherwise leave column creation to users)
	if (columns_column_number(columns) == 1) {
		if (GTK_IS_COLUMN(list_cols->data)) {
			column_t *col = GTK_COLUMN(list_cols->data);

			GtkAllocation allocation;
			gtk_widget_get_allocation(GTK_WIDGET(col), &allocation);

			if (allocation.width > 1000) {
				column_t *new_col = column_new(0);
				columns_add_after(columns, col, new_col, true);
				r = new_in_column(columns, new_col, buffer);
				goto heuristic_new_frame_return;
			}
		}
	}

	// if we are opening a buffer and it isn't the null buffer then search for an editor displaying the null buffer to take over
	if ((buffer != null_buffer()) && buffer != NULL) {
		editor_t *editor;
		find_editor_for_buffer(null_buffer(), NULL, &r, &editor);
		//printf("\teditor for null buffer: %p\n", editor);
		if (editor != NULL) {
			editor_switch_buffer(editor, buffer);
			goto heuristic_new_frame_return;
		}
	}

	{ // create a new row inside the active column (or the rightmost column if we are opening garbage)
		bool garbage = (buffer != NULL)
			&& ((buffer->path[0] == '+')
				|| (buffer->path[strlen(buffer->path) - 1] == '/'));
		column_t *destcol = NULL;

		if (!garbage) destcol = columns_active(columns);

		if (destcol == NULL) {
			GList *cur = g_list_last(list_cols);
			while (cur != NULL) {
				GtkAllocation allocation;
				gtk_widget_get_allocation(cur->data, &allocation);
				if (allocation.width >= 200) break;
				cur = cur->prev;
			}
			if (cur != NULL) destcol = cur->data;
		}

		if (destcol != NULL) {
			r = new_in_column(columns, destcol, buffer);
			goto heuristic_new_frame_return;
		}
	}

	// couldn't make a new frame, we fail

heuristic_new_frame_return:
	g_list_free(list_cols);
	return r;
}

void columns_column_remove(columns_t *columns, column_t *col, tframe_t *frame, bool reparenting, bool close_column) {
	if (column_frame_number(col) == 1) {
		if (columns_column_number(columns) == 1) {
			quick_message("Error", "Can not close last frame of the window\n");
			return;
		}

		if (!close_column) return;

		if (column_remove(col, frame, reparenting, true)) {
			columns_remove(columns, col);
		}
	} else {
		column_remove(col, frame, reparenting, false);
	}
}
