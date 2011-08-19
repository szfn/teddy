#include "editors.h"

#include <math.h>

static editor_t **editors;
static GtkWidget **resize_elements;
static int editors_allocated;
static GtkWidget *editors_window;
static GtkWidget *editors_vbox;
static int after_show = 0;
static int empty = 1;

void editors_init(GtkWidget *window) {
    int i;
    
    editors_allocated = 10;
    editors = malloc(sizeof(editor_t *) * editors_allocated);
    if (!editors) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    resize_elements = malloc(sizeof(GtkWidget *) * editors_allocated);
    if (!resize_elements) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < editors_allocated; ++i) {
        editors[i] = NULL;
        resize_elements[i] = NULL;
    }

    editors_vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), editors_vbox);

    editors_window = window;
}

void editors_free(void) {
    int i;
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] != NULL) {
            editor_free(editors[i]);
            editors[i] = NULL;
        }
    }
    free(editors);
    free(resize_elements);
}

static void editors_grow(void) {
    int i;
    
    editors = realloc(editors, sizeof(editor_t *) * editors_allocated * 2);
    if (!editors) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    resize_elements = realloc(resize_elements, sizeof(GtkWidget *) * editors_allocated * 2);
    for (i = editors_allocated; i < editors_allocated * 2; ++i) {
        editors[i] = NULL;
        resize_elements[i] = NULL;
    }
    editors_allocated *= 2;
}

static void editors_adjust_size(void) {
    int total_allocated_height = 0;
    GtkAllocation allocation;
    int i;

    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) continue;
        total_allocated_height += (editors[i]->allocated_vertical_space + 10);
    }

    gtk_widget_get_allocation(editors_vbox, &allocation);

    printf("Total space needed for editors %d, allocated space %d\n", total_allocated_height, allocation.height);

    if (total_allocated_height > allocation.height) {
        int difference = total_allocated_height - allocation.height;

        for (i = 0; i < editors_allocated; ++i) {
            if (editors[i] == NULL) continue;
            editors[i]->allocated_vertical_space -= (int)ceil(difference * (double)(editors[i]->allocated_vertical_space / total_allocated_height));
            if (editors[i]->allocated_vertical_space < 50) {
                editors[i]->allocated_vertical_space = 50;
            }
        }
    }

    printf("Height requests:\n");
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) continue;
        printf("   vspace: %d\n", editors[i]->allocated_vertical_space);
        gtk_widget_set_size_request(editors[i]->table, 10, editors[i]->allocated_vertical_space);
    }
}
 
void editors_add(editor_t *editor) {
    int i;
    
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) break;
    }

    if (i < editors_allocated) {
        editors[i] = editor;

        if (!empty) {
            GtkWidget *resize_element = gtk_drawing_area_new();
            gtk_widget_set_size_request(resize_element, 1, 8);
            resize_elements[i] = resize_element;
            gtk_container_add(GTK_CONTAINER(editors_vbox), resize_element);
            gtk_box_set_child_packing(GTK_BOX(editors_vbox), resize_element, FALSE, FALSE, 0, GTK_PACK_START);
        } else {
            resize_elements[i] = NULL;
        }

        gtk_container_add(GTK_CONTAINER(editors_vbox), editor->table);
        
        gtk_box_set_child_packing(GTK_BOX(editors_vbox), editor->table, empty ? TRUE : FALSE, empty ? TRUE : FALSE, 0, GTK_PACK_START);
        empty = 0;

        editor->allocated_vertical_space = editor_get_height_request(editor);

        if (after_show) {
            editors_adjust_size();
            editor_post_show_setup(editor);
            if (resize_elements[i] != NULL) {
                gdk_window_set_cursor(gtk_widget_get_window(resize_elements[i]), gdk_cursor_new(GDK_DOUBLE_ARROW));
            }
        }
    } else {
        editors_grow();
        editors_add(editor);
        return;
    }
}

editor_t *editors_new(buffer_t *buffer) {
    editor_t *e = new_editor(editors_window, buffer);

    editors_add(e);

    return e;
}

editor_t *editors_find_buffer_editor(buffer_t *buffer) {
    int i;
    
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) continue;
        if (editors[i]->buffer == buffer) return editors[i];
    }
    return NULL;
}

void editors_post_show_setup(void) {
    int i = 0;
    for (i = 0; i < editors_allocated; ++i) {
        if (resize_elements[i] != NULL) {
            gdk_window_set_cursor(gtk_widget_get_window(resize_elements[i]), gdk_cursor_new(GDK_DOUBLE_ARROW));
        }
        if (editors[i] == NULL) continue;
        editor_post_show_setup(editors[i]);
    }
    editors_adjust_size();
    after_show = 1;
}
