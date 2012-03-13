#include "column.h"

#include "global.h"
#include "buffers.h"

#include <math.h>
#include <assert.h>

#define MAGIC_NUMBER 18

static void gtk_column_class_init(column_class *klass);
static void gtk_column_init(column_t *column);
static void gtk_column_size_request(GtkWidget *widget, GtkRequisition *requisition);
static void gtk_column_size_allocate(GtkWidget *widget, GtkAllocation *allocation);

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

static gboolean editors_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, column_t *column) {
	column->exposed = 1;
	return FALSE;
}

column_t *column_new(GtkWidget *window, gint spacing) {
	GtkWidget *column_widget = g_object_new(GTK_TYPE_COLUMN, NULL);
	column_t *column = GTK_COLUMN(column_widget);

	GTK_BOX(column)->spacing = spacing;
	GTK_BOX(column)->homogeneous = FALSE;

	column->exposed = 0;
	column->editors_allocated = 10;
	column->editors = malloc(sizeof(editor_t *) * column->editors_allocated);
	column->fraction = 1.0;
	if (!(column->editors)) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < column->editors_allocated; ++i) {
		column->editors[i] = NULL;
	}

	g_signal_connect(G_OBJECT(column), "expose-event", G_CALLBACK(editors_expose_event_callback), (gpointer)column);

	column->editors_window = window;

	return column;
}

static void gtk_column_size_request(GtkWidget *widget, GtkRequisition *requisition) {
	GtkBox *box = GTK_BOX(widget);
	column_t *column = GTK_COLUMN(widget);

	requisition->width = GTK_CONTAINER(box)->border_width * 2;
	requisition->height = GTK_CONTAINER(box)->border_width * 2;

	requisition->width += 50;

	for (int i = 0; i < column->editors_allocated; ++i) {
		editor_t *editor = column->editors[i];
		if (editor == NULL) continue;
		requisition->height += (MIN_LINES_HEIGHT_REQUEST + 2) * editor->buffer->line_height;
		GtkRequisition child_requisition;
		gtk_widget_size_request(editor->container, &child_requisition);
	}

	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		children = children->next;

		requisition->height += child->padding * 2;
	}
}

static int editors_editor_from_table(column_t *column, GtkWidget *table) {
	int i;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) continue;
		if (column->editors[i]->container == table) return i;
	}
	return -1;
}

static void gtk_column_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
	column_t *column = GTK_COLUMN(widget);
	GtkBox *box = GTK_BOX(widget);

	widget->allocation = *allocation;

	double total_height_request = 0.0;
	for (int i = 0; i < column->editors_allocated; ++i) {
		editor_t *editor = column->editors[i];
		if (editor == NULL) continue;

		GtkRequisition child_requisition;
		gtk_widget_size_request(editor->container, &child_requisition);

		total_height_request += child_requisition.height;
	}

	gint y = allocation->y + GTK_CONTAINER(box)->border_width;

	GList *children = box->children;
	while (children) {
		GtkBoxChild *child = children->data;
		children = children->next;

		//int i = editors_editor_from_table(column, child->widget);

		GtkRequisition child_requisition;
		gtk_widget_size_request(child->widget, &child_requisition);

		double size_request = child_requisition.height / total_height_request;

		GtkAllocation child_allocation;

		child_allocation.x = allocation->x + GTK_CONTAINER(box)->border_width;
		child_allocation.width = MAX(1, (gint)allocation->width - (gint)GTK_CONTAINER(box)->border_width * 2);
		child_allocation.y = y;

		child_allocation.height = allocation->height * size_request;

		y += child_allocation.height;

		//printf("Allocated height: %d y: %d (%g)\n", child_allocation.height, child_allocation.y, size_request);

		gtk_widget_size_allocate(child->widget, &child_allocation);
	}

	//printf("\n");
}

void column_free(column_t *column) {
	int i;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] != NULL) {
			editor_free(column->editors[i]);
			column->editors[i] = NULL;
		}
	}
	free(column->editors);
	//g_object_unref(G_OBJECT(column));
}

static void editors_grow(column_t *column) {
	int i;

	column->editors = realloc(column->editors, sizeof(editor_t *) * column->editors_allocated * 2);
	if (!(column->editors)) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
	for (i = column->editors_allocated; i < column->editors_allocated * 2; ++i) {
		column->editors[i] = NULL;
	}
	column->editors_allocated *= 2;
}


static int editors_find_editor(column_t *column, editor_t *editor) {
	int i;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == editor) return i;
	}
	return -1;
}

static editor_t *editors_index_to_editor(column_t *column, int idx) {
	return (idx != -1) ? column->editors[idx] : NULL;
}

editor_t *column_get_editor_before(column_t *column, editor_t *editor) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	GList *prev = NULL, *cur;
	editor_t *r;

	for (cur = list; cur != NULL; cur = cur->next) {
		if (cur->data == editor->container) break;
		prev = cur;
	}

	if (cur == NULL) return NULL;
	if (prev == NULL) return NULL;

	r = editors_index_to_editor(column, editors_editor_from_table(column, prev->data));

	g_list_free(list);

	return r;
}

static editor_t *column_get_last(column_t *column) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	GList *cur, *prev = NULL;
	GtkWidget *w;

	for (cur = list; cur != NULL; cur = cur->next) {
		prev = cur;
	}

	if (prev == NULL) return NULL;

	w = prev->data;

	g_list_free(list);

	{
		int idx = editors_editor_from_table(column, w);
		return editors_index_to_editor(column, idx);
	}
}

static int column_add_editor_to_array(column_t *column, editor_t *editor) {
	int i;

	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) break;
	}

	if (i < column->editors_allocated) {
		column->editors[i] = editor;
		return i;
	}

	editors_grow(column);
	return column_add_editor_to_array(column, editor);
}

static void column_add_after(column_t *column, editor_t *before_editor, editor_t *new_editor) {
	column_add_editor_to_array(column, new_editor);

	gtk_container_add(GTK_CONTAINER(column), new_editor->container);
	gtk_box_set_child_packing(GTK_BOX(column), new_editor->container, TRUE, TRUE, 1, GTK_PACK_START);

	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	int j = 0;
	for (GList *cur = list; cur != NULL; cur = cur->next, ++j) {
		if (before_editor->container == cur->data) break;
	}
	g_list_free(list);

	gtk_box_reorder_child(GTK_BOX(column), new_editor->container, j+1);

	gtk_widget_show_all(GTK_WIDGET(column));
	gtk_widget_queue_draw(GTK_WIDGET(column));
}

static int column_add(column_t *column, editor_t *editor) {
	editor_t *last_editor = column_get_last(column);

	column_add_editor_to_array(column, editor);

	if (last_editor != NULL) {
		GtkAllocation allocation;
		double last_editor_real_size;
		double new_height;

		gtk_widget_get_allocation(last_editor->container, &allocation);
		last_editor_real_size = editor_get_height_request(last_editor);

		if (allocation.height * 0.40 > allocation.height - last_editor_real_size) {
			new_height = allocation.height * 0.40;
		} else {
			new_height = allocation.height - last_editor_real_size;
		}

		if (new_height < 50) {
			// Not enough space
			return 0;
		}

		gtk_widget_set_size_request(editor->container, -1, new_height);
		gtk_widget_set_size_request(last_editor->container, -1, allocation.height - new_height);
	}

	gtk_container_add(GTK_CONTAINER(column), editor->container);
	gtk_box_set_child_packing(GTK_BOX(column), editor->container, TRUE, TRUE, 1, GTK_PACK_START);

	gtk_widget_show_all(GTK_WIDGET(column));
	gtk_widget_queue_draw(GTK_WIDGET(column));

	return 1;
}

editor_t *column_new_editor(column_t *column, buffer_t *buffer) {
	editor_t *e;

	e = new_editor(column->editors_window, column, buffer);

	if (!column_add(column, e)) {
		column_remove(column, e);
		return NULL;
	} else {
		return e;
	}
}

editor_t *column_new_editor_after(column_t *column, editor_t *editor, buffer_t *buffer) {
	editor_t *e = new_editor(column->editors_window, column, buffer);

	column_add_after(column, editor, e);
	return e;
}

void column_replace_buffer(column_t *column, buffer_t *buffer) {
	int i;

	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) continue;
		if (column->editors[i]->buffer == buffer) {
			editor_switch_buffer(column->editors[i], null_buffer());
		}
	}
}

int column_editor_count(column_t *column) {
	int i, count = 0;;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] != NULL) ++count;
	}
	return count;
}

editor_t *column_get_first_editor(column_t *column) {
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	int new_idx = editors_editor_from_table(column, list->data);
	editor_t *r = (new_idx != -1) ? column->editors[new_idx] : NULL;
	g_list_free(list);
	return r;
}

editor_t *column_get_last_editor(column_t *column) {
	editor_t *r = NULL;
	GList *list = gtk_container_get_children(GTK_CONTAINER(column));
	for (GList *cur = list; cur != NULL; cur = cur->next) {
		if (cur->next == NULL) {
			int new_idx = editors_editor_from_table(column, cur->data);
			r = (new_idx != -1) ? column->editors[new_idx] : NULL;
			break;
		}
	}
	g_list_free(list);
	return r;
}

editor_t *column_remove(column_t *column, editor_t *editor) {
	int idx = editors_find_editor(column, editor);

	if (column_editor_count(column) == 1) {
		quick_message(editor, "Error", "Can not remove last editor of the window");
		return editor;
	}

	editor->initialization_ended = 0;

	editor_t *editor_before = column_get_editor_before(column, editor);

	if (editor_before != NULL) {
		GtkAllocation before_allocation;
		GtkAllocation current_allocation;

		gtk_widget_get_allocation(editor->container, &current_allocation);
		gtk_widget_get_allocation(editor_before->container, &before_allocation);

		gtk_widget_set_size_request(editor_before->container, -1, before_allocation.height + current_allocation.height);
	}

	if (idx != -1) {
		gtk_container_remove(GTK_CONTAINER(column), editor->container);
		column->editors[idx] = NULL;
	}

	editor_free(editor);

	return column_get_first_editor(column);
}

int column_remove_others(column_t *column, editor_t *editor) {
	int i, count = 0;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) continue;
		if (column->editors[i] == editor) continue;
		column_remove(column, column->editors[i]);
		++count;
	}
	return count;
}

editor_t *column_find_buffer_editor(column_t *column, buffer_t *buffer) {
	int i;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) continue;
		if (column->editors[i]->buffer == buffer) return column->editors[i];
	}
	return NULL;
}

editor_t *column_get_editor_from_position(column_t *column, double x, double y, bool *ontag) {
	int i;
	for (i = 0; i < column->editors_allocated; ++i) {
		GtkAllocation allocation;
		if (column->editors[i] == NULL) continue;
		gtk_widget_get_allocation(column->editors[i]->container, &allocation);
		if (inside_allocation(x, y, &allocation)) {
			GtkAllocation drar_allocation;
			gtk_widget_get_allocation(column->editors[i]->drar, &drar_allocation);
			allocation.y = drar_allocation.y;
			*ontag = !inside_allocation(x, y, &allocation);
			return column->editors[i];
		}

	}
	return NULL;
}

double column_get_occupied_space(column_t *column) {
	int i;
	double r = 0.0;
	for (i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) continue;
		r += editor_get_height_request(column->editors[i]);
	}
	return r;
}

bool column_editor_exists(column_t *column, editor_t *editor) {
	for (int i = 0; i < column->editors_allocated; ++i) {
		if (column->editors[i] == NULL) continue;
		if (column->editors[i] == editor) return true;
	}
	return false;
}

void column_resize_editor_pair(editor_t *first, double first_height, editor_t *second, double second_height) {
	GtkAllocation allocation;

	gtk_widget_get_allocation(first->label, &allocation);
	double min_first_height = allocation.height + 5;

	gtk_widget_get_allocation(second->label, &allocation);
	double min_second_height = allocation.height + 5;

	if (first_height < min_first_height) first_height = min_first_height;
	if (second_height < min_second_height) second_height = min_second_height;

	gtk_widget_set_size_request(first->container, -1, first_height);
	gtk_widget_set_size_request(second->container, -1, second_height);
	gtk_widget_queue_draw(GTK_WIDGET(first->column));
}
