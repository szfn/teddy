#include "column.h"

#include "global.h"
#include "buffers.h"

#include <math.h>
#include <assert.h>


#define MAGIC_NUMBER 18

static gboolean editors_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, column_t *column) {
    column->exposed = 1;    
    return FALSE;
}

column_t *column_new(GtkWidget *window) {
    column_t *column = malloc(sizeof(column_t));
    int i;

    column->exposed = 0;
    column->editors_allocated = 10;
    column->editors = malloc(sizeof(editor_t *) * column->editors_allocated);
    if (!(column->editors)) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < column->editors_allocated; ++i) {
        column->editors[i] = NULL;
    }

    column->editors_vbox = gtk_vbox_new(FALSE, 1);
    g_signal_connect(G_OBJECT(column->editors_vbox), "expose-event", G_CALLBACK(editors_expose_event_callback), (gpointer)column);

    column->editors_window = window;

    return column;
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
    free(column);
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

static int editors_editor_from_table(column_t *column, GtkWidget *table) {
    int i;
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        if (column->editors[i]->table == table) return i;
    }
    return -1;
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
    GList *list = gtk_container_get_children(GTK_CONTAINER(column->editors_vbox));
    GList *prev = NULL, *cur;
    editor_t *r;

    for (cur = list; cur != NULL; cur = cur->next) {
        if (cur->data == editor->table) break;
        prev = cur;
    }

    if (cur == NULL) return NULL;
    if (prev == NULL) return NULL;

    r = editors_index_to_editor(column, editors_editor_from_table(column, prev->data));

    g_list_free(list);

    return r;
}

static editor_t *column_get_last(column_t *column) {
    GList *list = gtk_container_get_children(GTK_CONTAINER(column->editors_vbox));
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

static int column_add(column_t *column, editor_t *editor) {
    int i;
    
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) break;
    }

    if (i < column->editors_allocated) {
        editor_t *last_editor = column_get_last(column);
        
        column->editors[i] = editor;

        if (last_editor != NULL) {
            GtkAllocation allocation;
            double last_editor_real_size;
            double new_height;

            gtk_widget_get_allocation(last_editor->table, &allocation);
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

            gtk_widget_set_size_request(editor->table, -1, new_height);
            gtk_widget_set_size_request(last_editor->table, -1, allocation.height - new_height);
        }

        gtk_container_add(GTK_CONTAINER(column->editors_vbox), editor->table);
        gtk_box_set_child_packing(GTK_BOX(column->editors_vbox), editor->table, TRUE, TRUE, 1, GTK_PACK_START);

        gtk_widget_show_all(column->editors_vbox);
        gtk_widget_queue_draw(column->editors_vbox);

        return 1;
    } else {
        editors_grow(column);
        return column_add(column, editor);
    }
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

void column_replace_buffer(column_t *column, buffer_t *buffer) {
    int i;

    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        if (column->editors[i]->buffer == buffer) {
            column->editors[i]->buffer = null_buffer();
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
    GList *list = gtk_container_get_children(GTK_CONTAINER(column->editors_vbox));
    int new_idx = editors_editor_from_table(column, list->data);
    editor_t *r = (new_idx != -1) ? column->editors[new_idx] : NULL;
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

    if (idx != -1) {
        gtk_container_remove(GTK_CONTAINER(column->editors_vbox), editor->table);
        column->editors[idx] = NULL;
    }

    editor_free(editor);

    return column_get_first_editor(column);
}

editor_t *column_find_buffer_editor(column_t *column, buffer_t *buffer) {
    int i;
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        if (column->editors[i]->buffer == buffer) return column->editors[i];
    }
    return NULL;
}

editor_t *column_get_editor_from_position(column_t *column, double x, double y) {
    int i;
    for (i = 0; i < column->editors_allocated; ++i) {
        GtkAllocation allocation;
        if (column->editors[i] == NULL) continue;
        gtk_widget_get_allocation(column->editors[i]->table, &allocation);
        if ((x >= allocation.x)
            && (x <= allocation.x + allocation.width)
            && (y >= allocation.y)
            && (y <= allocation.y + allocation.height))
            return column->editors[i];
        
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
