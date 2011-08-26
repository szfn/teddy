#include "column.h"

#include "global.h"
#include "buffers.h"

#include <math.h>
#include <assert.h>


#define MAGIC_NUMBER 18

static void editors_adjust_size(column_t *column) {
    int total_allocated_height = 0;
    GtkAllocation allocation;
    int i, count;
    int coefficient;

    if (!(column->exposed)) return;

    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        total_allocated_height += (column->editors[i]->allocated_vertical_space + MAGIC_NUMBER);
    }

    gtk_widget_get_allocation(column->editors_vbox, &allocation);

    printf("Total space needed for editors %d, allocated space %d\n", total_allocated_height, allocation.height);

    coefficient = total_allocated_height;

    for (count = 0; (count < 4) && (total_allocated_height > allocation.height); ++count) {
        int difference = total_allocated_height - allocation.height;
        int new_coefficient = 0;
        int new_total_allocated_height = 0;

        printf("Removing: %d\n", difference);

        for (i = 0; i < column->editors_allocated; ++i) {
            if (column->editors[i] == NULL) continue;
            if (column->editors[i]->allocated_vertical_space > 50) {
                int cut = (int)ceil(difference * ((double)(column->editors[i]->allocated_vertical_space+MAGIC_NUMBER) / coefficient));
                printf("   cutting: %d\n", cut);
                column->editors[i]->allocated_vertical_space -= cut;
                if (column->editors[i]->allocated_vertical_space < 50) {
                    column->editors[i]->allocated_vertical_space = 50;
                }

                new_coefficient += column->editors[i]->allocated_vertical_space + MAGIC_NUMBER;
            }
            new_total_allocated_height += column->editors[i]->allocated_vertical_space + MAGIC_NUMBER;
        }

        total_allocated_height = new_total_allocated_height;
        coefficient = new_coefficient;
    }

    printf("Height requests:\n");
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        printf("   vspace: %d\n", column->editors[i]->allocated_vertical_space);
        gtk_widget_set_size_request(column->editors[i]->table, 10, column->editors[i]->allocated_vertical_space);
    }
}

static gboolean editors_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    column_t *column = (column_t *)data;
    if (!(column->exposed)) {
        column->exposed = 1;
        editors_adjust_size(column);
    } 
    return FALSE;
}

column_t *column_new(GtkWidget *window, GtkWidget *container) {
    column_t *column = malloc(sizeof(column_t));
    int i;

    column->empty = 1;
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

    column->editors_vbox = gtk_vbox_new(FALSE, 0);
    g_signal_connect(G_OBJECT(column->editors_vbox), "expose-event", G_CALLBACK(editors_expose_event_callback), (gpointer)column);
    gtk_container_add(GTK_CONTAINER(container), column->editors_vbox);

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

/*
static gboolean resize_button_press_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    column_t *column = (column_t *)data;
    printf("Starting resize\n");
    column->frame_resize_origin = event->y;
    return TRUE;
    }*/

static int editors_editor_from_table(column_t *column, GtkWidget *table) {
    int i;
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        if (column->editors[i]->table == table) return i;
    }
    return -1;
}

/*
static editor_t *editors_index_to_editor(column_t *column, int idx) {
    return (idx != -1) ? column->editors[idx] : NULL;
}


static gboolean resize_button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    column_t *column = (column_t *)data;
    double change = event->y - column->frame_resize_origin;
    GList *prev = NULL;
    GList *list = gtk_container_get_children(GTK_CONTAINER(column->editors_vbox));
    GList *list_head = list;

    for ( ; list != NULL; list = list->next) {
        if (list->data == widget) {
            break;
        }
        prev = list;
    }

    if (list == NULL) {
        printf("Resize targets not found\n");
        return TRUE;
    } 

    {
        editor_t *preved = editors_index_to_editor(column, editors_editor_from_table(column, (GtkWidget *)(prev->data)));
        editor_t *nexted = editors_index_to_editor(column, editors_editor_from_table(column, (GtkWidget *)(list->next->data)));
        GtkAllocation allocation;

        if (preved == NULL) {
            printf("Resize previous editor target not found\n");
            return TRUE;
        }

        if (nexted == NULL) {
            printf("Resize next editor target not found\n");
            return TRUE;
        }

        nexted->allocated_vertical_space -= change;
        if (nexted->allocated_vertical_space < 50) nexted->allocated_vertical_space = 50;

        gtk_widget_get_allocation(preved->table, &allocation);

        if (allocation.height > preved->allocated_vertical_space) {
            double new_height = allocation.height += change;
            if (new_height < preved->allocated_vertical_space) {
                preved->allocated_vertical_space = new_height;
            }
        } else {
            preved->allocated_vertical_space += change;
        }
        
        editors_adjust_size(column);
        gtk_widget_queue_draw(column->editors_vbox);
    }

    g_list_free(list_head);
    
    return TRUE;
}

static gboolean resize_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    gdk_window_set_cursor(gtk_widget_get_window(widget), gdk_cursor_new(GDK_DOUBLE_ARROW));
    return TRUE;
    }*/


void column_add(column_t *column, editor_t *editor) {
    int i;
    
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) break;
    }

    if (i < column->editors_allocated) {
        column->editors[i] = editor;

        gtk_container_add(GTK_CONTAINER(column->editors_vbox), editor->table);
        
        gtk_box_set_child_packing(GTK_BOX(column->editors_vbox), editor->table, column->empty ? TRUE : FALSE, column->empty ? TRUE : FALSE, 0, GTK_PACK_START);
        column->empty = 0;

        editor->allocated_vertical_space = editor_get_height_request(editor);

        editors_adjust_size(column);

        gtk_widget_show_all(column->editors_vbox);
        gtk_widget_queue_draw(column->editors_vbox);
    } else {
        editors_grow(column);
        column_add(column, editor);
        return;
    }
}

editor_t *column_new_editor(column_t *column, buffer_t *buffer) {
    editor_t *e;

    e = new_editor(column->editors_window, column, buffer);

    column_add(column, e);

    return e;
}


void column_replace_buffer(column_t *column, buffer_t *buffer) {
    int i;
    buffer_t *replacement_buffer = NULL;

    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == NULL) continue;
        if (column->editors[i]->buffer == buffer) {
            if (replacement_buffer == NULL) replacement_buffer = buffers_get_replacement_buffer(buffer);
            editor_switch_buffer(column->editors[i], replacement_buffer);
        }
    }
}

static int editors_count(column_t *column) {
    int i, count = 0;;
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] != NULL) ++count;
    }
    return count;
}

static int editors_find_editor(column_t *column, editor_t *editor) {
    int i;
    for (i = 0; i < column->editors_allocated; ++i) {
        if (column->editors[i] == editor) return i;
    }
    return -1;
}

editor_t *column_remove(column_t *column, editor_t *editor) {
    int idx = editors_find_editor(column, editor);
    
    if (editors_count(column) == 1) {
        quick_message(editor, "Error", "Can not remove last editor of the window");
        return editor;
    }

    editor->initialization_ended = 0;

    if (idx != -1) {
        GList *list;
        gtk_container_remove(GTK_CONTAINER(column->editors_vbox), editor->table);
        column->editors[idx] = NULL;

        // set the first element of the vbox (that may have changed) to EXPAND and FILL
        list = gtk_container_get_children(GTK_CONTAINER(column->editors_vbox));
        gtk_box_set_child_packing(GTK_BOX(column->editors_vbox), list->data, TRUE, TRUE, 0, GTK_PACK_START);        
        g_list_free(list);
        
    }

    editor_free(editor);

    {
        GList *list = gtk_container_get_children(GTK_CONTAINER(column->editors_vbox));
        int new_idx = editors_editor_from_table(column, list->data);
        editor_t *r = (new_idx != -1) ? column->editors[new_idx] : NULL;
        g_list_free(list);
        return r;
    }
}

