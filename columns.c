#include "columns.h"

#include "column.h"

column_t **columns;
int columns_allocated;
GtkWidget *columns_window;
GtkWidget *columns_hbox;

/* TODO:
   - hbox to actually support more than one column (all columns with the same size(
   - appropriate initial sizing of columns
   - user column resizing
 */

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

editor_t *columns_new(buffer_t *buffer) {
    int i;
    
    for (i = 0; i < columns_allocated; ++i) {
        if (columns[i] == NULL) break;
    }

    if (i < columns_allocated) {
        columns[i] = column_new(columns_window, columns_hbox);
        gtk_box_set_child_packing(GTK_BOX(columns_hbox), columns[i]->editors_vbox, TRUE, TRUE, 1, GTK_PACK_START);
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
