#include "columns.h"

#include "column.h"
#include "global.h"
#include "buffers.h"

static void gtk_columns_class_init(columns_class *klass);
static void gtk_columns_init(columns_t *columns);
static void gtk_columns_size_request(GtkWidget *widget, GtkRequisition *requisition);
static void gtk_columns_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

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

columns_t *the_columns_new(GtkWidget *window) {
	GtkWidget *columns_widget = g_object_new(GTK_TYPE_COLUMNS, NULL);
	columns_t *columns = GTK_COLUMNS(columns_widget);

	GTK_BOX(columns)->spacing = 0;
	GTK_BOX(columns)->homogeneous = FALSE;

	columns->active_column = NULL;
	columns->columns_allocated = 5;
	columns->columns = malloc(columns->columns_allocated * sizeof(column_t *));
	if (!columns->columns) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < columns->columns_allocated; ++i) {
		columns->columns[i] = NULL;
	}

	columns->columns_window = window;

	gtk_container_add(GTK_CONTAINER(columns->columns_window), GTK_WIDGET(columns));

	//g_signal_connect(G_OBJECT(columns_hbox), "expose-event", G_CALLBACK(columns_expose_callback), NULL);

	return columns;
}

static void gtk_columns_size_request(GtkWidget *widget, GtkRequisition *requisition) {
	GtkBox *box = GTK_BOX(widget);
	columns_t *columns = GTK_COLUMNS(widget);

	requisition->width = 1;
	requisition->height = 1;

	for (int i = 0; i < columns->columns_allocated; ++i) {
		column_t *column = columns->columns[i];
		if (column == NULL) continue;
		GtkRequisition child_requisition;
		gtk_widget_size_request(GTK_WIDGET(column), &child_requisition);
		requisition->width += child_requisition.width;
		requisition->height = MAX(requisition->height, child_requisition.height);
	}

	requisition->width += GTK_CONTAINER(box)->border_width * 2;
	requisition->height += GTK_CONTAINER(box)->border_width * 2;
}

static void gtk_columns_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
	columns_t *columns = GTK_COLUMNS(widget);
	GtkBox *box = GTK_BOX(widget);

	widget->allocation = *allocation;

	double fraction_total = 0.0;
	for (int i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] != NULL) fraction_total += columns->columns[i]->fraction;
	}

	double x = allocation->x + GTK_CONTAINER(box)->border_width;

	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		children = children->next;

		GtkAllocation child_allocation;

		column_t *column = GTK_COLUMN(child->widget);

		child_allocation.y = allocation->y + GTK_CONTAINER(box)->border_width;
		child_allocation.x = x;
		child_allocation.height = MAX(1, (gint)allocation->height - (gint)GTK_CONTAINER(box)->border_width * 2);
		child_allocation.width = column->fraction/fraction_total * allocation->width;

		x += child_allocation.width;

		gtk_widget_size_allocate(child->widget, &child_allocation);
	}
}

static void columns_grow(columns_t *columns) {
	columns->columns = realloc(columns->columns, columns->columns_allocated * 2 * sizeof(column_t *));
	if (!(columns->columns)) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}

	for(int i = columns->columns_allocated; i < columns->columns_allocated * 2; ++i) {
		columns->columns[i] = NULL;
	}

	columns->columns_allocated *= 2;
}

static column_t *columns_get_last(columns_t *columns) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	GList *cur;
	GtkWidget *w;
	int i;

	if (list == NULL) return NULL;

	for (cur = list; cur->next != NULL; cur = cur->next);

	w = cur->data;

	g_list_free(list);

	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		if (GTK_WIDGET(columns->columns[i]) == w) return columns->columns[i];
	}

	return NULL;
}

static column_t *columns_widget_to_column(columns_t *columns, GtkWidget *w) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		if (GTK_WIDGET(columns->columns[i]) == w) break;
	}

	return (i >= columns->columns_allocated) ? NULL : columns->columns[i];
}

static column_t *columns_get_first(columns_t *columns) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	GtkWidget *w = list->data;
	g_list_free(list);
	return columns_widget_to_column(columns, w);
}

editor_t *columns_new(columns_t *columns, buffer_t *buffer) {
	int i;

	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) break;
	}

	if (i < columns->columns_allocated) {
		column_t *last_column = columns_get_last(columns);

		columns->columns[i] = column_new(columns->columns_window, 0);
		gtk_box_set_child_packing(GTK_BOX(columns), GTK_WIDGET(columns->columns[i]), TRUE, TRUE, 1, GTK_PACK_START);

		//printf("Resize mode: %d\n", gtk_container_get_resize_mode(GTK_CONTAINER(columns_hbox)));

		if (last_column != NULL) {
			GtkAllocation allocation;

			gtk_widget_get_allocation(GTK_WIDGET(last_column), &allocation);

			if (allocation.width * 0.40 < 50) {
				columns_remove(columns, columns->columns[i], NULL);
				return column_get_first_editor(columns_get_first(columns));
			}

			if (allocation.width > 1) {
				double f =  last_column->fraction;
				last_column->fraction = f * 0.60;
				columns->columns[i]->fraction = f * 0.40;
			}
		}

		gtk_container_add(GTK_CONTAINER(columns), GTK_WIDGET(columns->columns[i]));

		return column_new_editor(columns->columns[i], buffer);
	} else {
		columns_grow(columns);
		return columns_new(columns, buffer);
	}
}

void columns_free(columns_t *columns) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		column_free(columns->columns[i]);
		columns->columns[i] = NULL;
	}
	//g_object_unref(G_OBJECT(columns));
}

void columns_replace_buffer(columns_t *columns, buffer_t *buffer) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		column_replace_buffer(columns->columns[i], buffer);
	}
}

column_t *columns_get_column_before(columns_t *columns, column_t *column) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	GList *cur;
	GList *prev = NULL;
	GtkWidget *w;

	for (cur = list; cur != NULL; cur = cur->next) {
		if (cur->data == column) break;
		prev = cur;
	}

	if (cur == NULL) return NULL;
	if (prev == NULL) return NULL;

	w = prev->data;

	g_list_free(list);

	return columns_widget_to_column(columns, w);
}


int columns_column_count(columns_t *columns) {
	int i, count = 0;
	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		++count;
	}
	return count;
}

static int columns_find_column(columns_t *columns, column_t *column) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == column) return i;
	}
	return -1;
}

column_t *columns_remove(columns_t *columns, column_t *column, editor_t *editor) {
	int idx = columns_find_column(columns, column);

	if (columns_column_count(columns) == 1) {
		if (editor != NULL) {
			quick_message(editor, "Error", "Can not remove last column of the window");
		}
		return column;
	}

	if (idx != -1) {
		column_t *column_to_grow = columns_get_column_before(columns, column);

		columns->columns[idx] = NULL;

		if (column_to_grow == NULL) {
			column_to_grow = columns_get_first(columns);
		}

		if (column_to_grow != NULL) {
			column_to_grow->fraction += column->fraction;
		}
	}

	gtk_container_remove(GTK_CONTAINER(columns), GTK_WIDGET(column));

	column_free(column);

	return columns_get_first(columns);
}

void columns_remove_others(columns_t *columns, column_t *column, editor_t *editor) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		if (columns->columns[i] == column) continue;
		columns_remove(columns, columns->columns[i], editor);
	}
}

editor_t *columns_get_buffer(columns_t *columns, buffer_t *buffer) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		editor_t *r;
		if (columns->columns[i] == NULL) continue;
		r = column_find_buffer_editor(columns->columns[i], buffer);
		if (r != NULL) return r;
	}
	return NULL;
}

column_t *columns_get_column_from_position(columns_t *columns, double x, double y) {
	int i;
	for (i = 0; i < columns->columns_allocated; ++i) {
		GtkAllocation allocation;
		if (columns->columns[i] == NULL) continue;
		gtk_widget_get_allocation(GTK_WIDGET(columns->columns[i]), &allocation);
		//printf("Comparing (%g,%g) with (%d,%d) (%d,%d)\n", x, y, allocation.x, allocation.y, allocation.x+allocation.width, allocation.y+allocation.height);
		if ((x >= allocation.x)
			&& (x <= allocation.x + allocation.width)
			&& (y >= allocation.y)
			&& (y <= allocation.y + allocation.height))
			return columns->columns[i];
	}
	return NULL;
}

editor_t *columns_get_editor_from_position(columns_t *columns, double x, double y) {
	column_t *column = columns_get_column_from_position(columns, x, y);
	if (column == NULL) {
		printf("Column not found\n");
		return NULL;
	}
	return column_get_editor_from_position(column, x, y);
}

void columns_swap_columns(columns_t *columns, column_t *cola, column_t *colb) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	GList *cur;
	int idxa = -1, idxb = -1, i;
	for (cur = list, i = 0; cur != NULL; cur = cur->next, ++i) {
		if (cur->data == cola) {
			idxa = i;
		}
		if (cur->data == colb) {
			idxb = i;
		}
	}
	g_list_free(list);

	if (idxa == -1) return;
	if (idxb == -1) return;

	gtk_box_reorder_child(GTK_BOX(columns), GTK_WIDGET(cola), idxb);
	gtk_box_reorder_child(GTK_BOX(columns), GTK_WIDGET(colb), idxa);

	double temp = cola->fraction;
	cola->fraction = colb->fraction;
	colb->fraction = temp;
}

static column_t **columns_ordered_columns(columns_t *columns, int *numcol) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(columns));
	column_t **r = malloc(sizeof(column_t *) * g_list_length(list));
	GList *cur;
	int i = 0;

	*numcol = g_list_length(list);

	for (cur = list; cur != NULL; cur = cur->next) {
		r[i] = GTK_COLUMN(cur->data);
		++i;
	}

	g_list_free(list);

	return r;
}

editor_t *heuristic_new_frame(columns_t *columns, editor_t *spawning_editor, buffer_t *buffer) {
	int garbage = (buffer->name[0] == '+'); // when the garbage flag is set we want to put the buffer in a frame to the right
	int numcol;
	column_t **ordered_columns = columns_ordered_columns(columns, &numcol);
	editor_t *retval = NULL;

	//printf("New Buffer Heuristic\n");

	{
		int i;
		for (i = 0; i < numcol; ++i) {
			if (ordered_columns[i] == NULL) {
				//printf("There were unexpected errors, bailing out of new heuristic\n");
				goto heuristic_new_frame_exit;
			}
		}
	}

	if (null_buffer() != buffer) { // not the +null+ buffer
		int i = garbage ? numcol-1 : 0;

		// search for an editor pointing at +null+, direction of search determined by garbage flag

		while (garbage ? (i >= 0) : (i < numcol)) {
			editor_t *editor = column_find_buffer_editor(ordered_columns[i], null_buffer());
			if (editor != NULL) {
				editor_switch_buffer(editor, buffer);
				retval = editor;
				goto heuristic_new_frame_exit;
			}
			i += (garbage ? -1 : +1);
		}

		//printf("   No +null+ editor to take over\n");
	}

	{ // if the last column is very large create a new column and use it
		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(ordered_columns[numcol-1]), &allocation);

		if (allocation.width > 1000) {
			editor_t *editor = columns_new(columns, buffer);
			if (editor != NULL) {
				retval = editor;
				goto heuristic_new_frame_exit;
			}
		}

		//printf("   No large column to split\n");
	}

	{ // search for the column with the most space on the bottom editor
		column_t *best_column = NULL;
		int best_column_last_editor_height = 50;

		for (int i = 0; i < numcol; ++i) {
			GtkAllocation allocation;
			editor_t *editor = column_get_last_editor(ordered_columns[i]);
			gtk_widget_get_allocation(editor->drar, &allocation);

			if (allocation.height > best_column_last_editor_height) {
				best_column_last_editor_height = allocation.height;
				best_column = ordered_columns[i];
				if (allocation.height > (MAX_LINES_HEIGHT_REQUEST / 2) * buffer->line_height) {
					break;
				}
			}
		}

		if (best_column != NULL) {
			editor_t *editor = column_new_editor(best_column, buffer);
			if (editor != NULL) {
				retval = editor;
				goto heuristic_new_frame_exit;
			}
		}
	}

	// try to create a new frame inside the active column (column of last edit operation)
	if (columns->active_column != NULL) {
		editor_t *editor = column_new_editor(columns->active_column, buffer);
		if (editor != NULL) {
			retval = editor;
			goto heuristic_new_frame_exit;
		}
		//printf("   Splitting of active column failed\n");
	} else {
		//printf("   Active column isn't set\n");
	}


	{ // no good place was found, see if it's appropriate to open a new column
		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(ordered_columns[numcol-1]), &allocation);

		if ((allocation.width * 0.40) > (30 * buffer->em_advance)) {
			editor_t *editor = columns_new(columns, buffer);
			if (editor != NULL) {
				retval = editor;
				goto heuristic_new_frame_exit;
			}
			//printf("   Couldn't open a new column\n");
		} else {
			//printf("   Opening a new column is not appropriate: %g %g\n", allocation.width * 0.40, 30 * buffer->em_advance);
		}
	}

	{ // no good place was found AND it wasn't possible to open a column
		int i = garbage ? numcol-1 : 0;
		while (garbage ? (i >= 0) : (i < numcol)) {
			editor_t *editor = column_new_editor(ordered_columns[i], buffer);
			if (editor != NULL) {
				//printf("   New editor\n");
				retval = editor;
				goto heuristic_new_frame_exit;
			}
			i += (garbage ? -1 : +1);
		}

		//printf("   Couldn't open a new row anywhere\n");
	}

	// no good place exists for this buffer, if we aren't trying to open a +null+ buffer we are authorized to take over the spawning editor
	if (buffer != null_buffer()) {
		//printf("   Taking over current editor\n");
		editor_switch_buffer(spawning_editor, buffer);
		gtk_widget_queue_draw(spawning_editor->container);
		retval = spawning_editor;
		goto heuristic_new_frame_exit;
	}

 heuristic_new_frame_exit:
	//printf("   Done\n");
	free(ordered_columns);
	return retval;
}

bool editor_exists(columns_t *columns, editor_t *editor) {
	for (int i = 0; i < columns->columns_allocated; ++i) {
		if (columns->columns[i] == NULL) continue;
		bool r = column_editor_exists(columns->columns[i], editor);
		if (r) return true;
	}
	return false;
}

void columns_reallocate(columns_t *columns) {
	gtk_columns_size_allocate(GTK_WIDGET(columns), &(GTK_WIDGET(columns)->allocation));
}
