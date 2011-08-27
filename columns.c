#include "columns.h"

#include "column.h"
#include "global.h"

column_t **columns;
int columns_allocated;
GtkWidget *columns_window;
GtkWidget *columns_hbox;

//static int columns_exposed = 0;

/* TODO:
   - user column resizing
 */

/*static void columns_adjust_size(void) {
    GList *list;
    
    if (!columns_exposed) return;

    list = gtk_container_get_children(GTK_CONTAINER(columns_hbox));

    if (list != NULL) {
        gtk_box_set_child_packing(GTK_BOX(columns_hbox), list->data, TRUE, TRUE, 1, GTK_PACK_START);
    }

    g_list_free(list);
    }

static gboolean columns_expose_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    GList *list = gtk_container_get_children(GTK_CONTAINER(columns_hbox));
    GList *cur;

    for (cur = list; cur != NULL; cur = cur->next) {
        gtk_widget_set_size_request(cur->data, 10, 10);
    }

    g_list_free(list);
    
    return FALSE;
    }*/

void columns_init(GtkWidget *window) {
    int i;
    
    columns_allocated = 5;
    columns = malloc(columns_allocated * sizeof(column_t *));
    if (!columns) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < columns_allocated; ++i) {
        columns[i] = NULL;
    }

    columns_hbox = gtk_hbox_new(FALSE, 1);
    columns_window = window;

    gtk_container_add(GTK_CONTAINER(window), columns_hbox);

    //g_signal_connect(G_OBJECT(columns_hbox), "expose-event", G_CALLBACK(columns_expose_callback), NULL);
}

static void columns_grow(void) {
    int i;
    
    columns = realloc(columns, columns_allocated * 2 * sizeof(column_t *));
    if (!columns) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }

    for(i = columns_allocated; i < columns_allocated * 2; ++i) {
        columns[i] = NULL;
    }

    columns_allocated *= 2;
}

static column_t *columns_get_last(void) {
    GList *list = gtk_container_get_children(GTK_CONTAINER(columns_hbox));
    GList *cur;
    GtkWidget *w;
    int i;

    if (list == NULL) return NULL;

    for (cur = list; cur->next != NULL; cur = cur->next);

    w = cur->data;

    g_list_free(list);

    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) continue;
        if (columns[i]->editors_vbox == w) return columns[i];
    }

    return NULL;
}

static column_t *columns_widget_to_column(GtkWidget *w) {
    int i;
    
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) continue;
        if (columns[i]->editors_vbox == w) break;
    }
    
    return (i >= columns_allocated) ? NULL : columns[i];
}

static column_t *columns_get_first(void) {
    GList *list = gtk_container_get_children(GTK_CONTAINER(columns_hbox));
    GtkWidget *w = list->data;
    g_list_free(list);
    return columns_widget_to_column(w);
}

editor_t *columns_new(buffer_t *buffer) {
    int i;
    
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) break;
    }

    if (i < columns_allocated) {
        column_t *last_column = columns_get_last();

        columns[i] = column_new(columns_window);
        gtk_box_set_child_packing(GTK_BOX(columns_hbox), columns[i]->editors_vbox, TRUE, TRUE, 1, GTK_PACK_START);

        //printf("Resize mode: %d\n", gtk_container_get_resize_mode(GTK_CONTAINER(columns_hbox)));
        
        if (last_column != NULL) {
            GtkAllocation allocation;

            gtk_widget_get_allocation(last_column->editors_vbox, &allocation);

            if (allocation.width * 0.40 < 100) {
                columns_remove(columns[i], NULL);
                return column_get_first_editor(columns_get_first());
            }
            
            if (allocation.width > 1) {
                gtk_widget_set_size_request(last_column->editors_vbox, allocation.width * 0.60, 10);
                gtk_widget_set_size_request(columns[i]->editors_vbox, allocation.width * 0.40, 10);
            }
        }

        gtk_container_add(GTK_CONTAINER(columns_hbox), columns[i]->editors_vbox);
        
        return column_new_editor(columns[i], buffer);
    } else {
        columns_grow();
        return columns_new(buffer);
    }
}

void columns_free(void) {
    int i;
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) continue;
        column_free(columns[i]);
        columns[i] = NULL;
    }
    free(columns);
}

void columns_replace_buffer(buffer_t *buffer) {
    int i;
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) continue;
        column_replace_buffer(columns[i], buffer);
    }
}

column_t *columns_get_column_before(column_t *column) {
    GList *list = gtk_container_get_children(GTK_CONTAINER(columns_hbox));
    GList *cur;
    GList *prev = NULL;
    GtkWidget *w;

    for (cur = list; cur != NULL; cur = cur->next) {
        if (cur->data == column->editors_vbox) break;
        prev = cur;
    }

    if (cur == NULL) return NULL;
    if (prev == NULL) return NULL;

    w = prev->data;

    g_list_free(list);

    return columns_widget_to_column(w);
}
 

int columns_column_count(void) {
    int i, count = 0;
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) continue;
        ++count;
    }
    return count;
}

static int columns_find_column(column_t *column) {
    int i;
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == column) return i;
    }
    return -1;
}

column_t *columns_remove(column_t *column, editor_t *editor) {
    int idx = columns_find_column(column);

    if (columns_column_count() == 1) {
        if (editor != NULL) {
            quick_message(editor, "Error", "Can not remove last column of the window");
        }
        return column;
    }

    if (idx != -1) {
        column_t *column_to_grow = columns_get_column_before(column);
        GtkAllocation removed_allocation;

        gtk_widget_get_allocation(column->editors_vbox, &removed_allocation);
        
        gtk_container_remove(GTK_CONTAINER(columns_hbox), column->editors_vbox);
        columns[idx] = NULL;

        if (column_to_grow == NULL) {
            column_to_grow = columns_get_first();
        }

        if (column_to_grow != NULL) {
            GtkAllocation allocation;
            gtk_widget_get_allocation(column_to_grow->editors_vbox, &allocation);

            allocation.width += removed_allocation.width;

            gtk_widget_set_size_request(column_to_grow->editors_vbox, allocation.width, -1);
        }
    }

    column_free(column);

    return columns_get_first();
}

editor_t *columns_get_buffer(buffer_t *buffer) {
    int i;
    for (i = 0; i < columns_allocated; ++i) {
        editor_t *r;
        if (columns[i] == NULL) continue;
        r = column_find_buffer_editor(columns[i], buffer);
        if (r != NULL) return r;
    }
    return NULL;
}

column_t *columns_get_column_from_position(double x, double y) {
    int i;
    for (i = 0; i < columns_allocated; ++i) {
        GtkAllocation allocation;
        if (columns[i] == NULL) continue;
        gtk_widget_get_allocation(columns[i]->editors_vbox, &allocation);
        printf("Comparing (%g,%g) with (%d,%d) (%d,%d)\n", x, y, allocation.x, allocation.y, allocation.x+allocation.width, allocation.y+allocation.height);
        if ((x >= allocation.x)
            && (x <= allocation.x + allocation.width)
            && (y >= allocation.y)
            && (y <= allocation.y + allocation.height))
            return columns[i];
    }
    return NULL;
}

editor_t *columns_get_editor_from_positioon(double x, double y) {
    column_t *column = columns_get_column_from_position(x, y);
    if (column == NULL) {
        printf("Column not found\n");
        return NULL;
    }
    return column_get_editor_from_position(column, x, y);
}

void columns_swap_columns(column_t *cola, column_t *colb) {
    GList *list = gtk_container_get_children(GTK_CONTAINER(columns_hbox));
    GList *cur;
    int idxa = -1, idxb = -1, i;
    for (cur = list, i = 0; cur != NULL; cur = cur->next, ++i) {
        if (cur->data == cola->editors_vbox) {
            idxa = i;
        }
        if (cur->data == colb->editors_vbox) {
            idxb = i;
        }
    }
    g_list_free(list);

    if (idxa == -1) return;
    if (idxb == -1) return;
    
    gtk_box_reorder_child(GTK_BOX(columns_hbox), cola->editors_vbox, idxb);
    gtk_box_reorder_child(GTK_BOX(columns_hbox), colb->editors_vbox, idxa);

    {
        GtkAllocation alla;
        GtkAllocation allb;

        gtk_widget_get_allocation(cola->editors_vbox, &alla);
        gtk_widget_get_allocation(colb->editors_vbox, &allb);

        gtk_widget_set_size_request(cola->editors_vbox, allb.width, -1);
        gtk_widget_set_size_request(colb->editors_vbox, alla.width, -1);

        gtk_widget_queue_draw(columns_hbox);
    }
}
