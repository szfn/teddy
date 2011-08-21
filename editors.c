#include "editors.h"

#include "global.h"

#include <math.h>

static editor_t **editors;
static GtkWidget **resize_elements;
static int editors_allocated;
static GtkWidget *editors_window;
static GtkWidget *editors_vbox;
static int empty = 1;
static int exposed = 0;

static double frame_resize_origin;

#define MAGIC_NUMBER 18

static void editors_adjust_size(void) {
    int total_allocated_height = 0;
    GtkAllocation allocation;
    int i, count;
    int coefficient;

    if (!exposed) return;

    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) continue;
        total_allocated_height += (editors[i]->allocated_vertical_space + MAGIC_NUMBER);
    }

    gtk_widget_get_allocation(editors_vbox, &allocation);

    printf("Total space needed for editors %d, allocated space %d\n", total_allocated_height, allocation.height);

    coefficient = total_allocated_height;

    for (count = 0; (count < 4) && (total_allocated_height > allocation.height); ++count) {
        int difference = total_allocated_height - allocation.height;
        int new_coefficient = 0;
        int new_total_allocated_height = 0;

        printf("Removing: %d\n", difference);

        for (i = 0; i < editors_allocated; ++i) {
            if (editors[i] == NULL) continue;
            if (editors[i]->allocated_vertical_space > 50) {
                int cut = (int)ceil(difference * ((double)(editors[i]->allocated_vertical_space+MAGIC_NUMBER) / coefficient));
                printf("   cutting: %d\n", cut);
                editors[i]->allocated_vertical_space -= cut;
                if (editors[i]->allocated_vertical_space < 50) {
                    editors[i]->allocated_vertical_space = 50;
                }

                new_coefficient += editors[i]->allocated_vertical_space + MAGIC_NUMBER;
            }
            new_total_allocated_height += editors[i]->allocated_vertical_space + MAGIC_NUMBER;
        }

        total_allocated_height = new_total_allocated_height;
        coefficient = new_coefficient;
    }

    printf("Height requests:\n");
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) continue;
        printf("   vspace: %d\n", editors[i]->allocated_vertical_space);
        gtk_widget_set_size_request(editors[i]->table, 10, editors[i]->allocated_vertical_space);
    }
}

static gboolean editors_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    if (!exposed) {
        exposed = 1;
        editors_adjust_size();
    } 
    return FALSE;
}

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
    g_signal_connect(G_OBJECT(editors_vbox), "expose-event", G_CALLBACK(editors_expose_event_callback), NULL);
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

static gboolean resize_button_press_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    printf("Starting resize\n");
    frame_resize_origin = event->y;
    return TRUE;
}

static editor_t *editors_editor_from_table(GtkWidget *table) {
    int i;
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) continue;
        if (editors[i]->table == table) return editors[i];
    }
    return NULL;
}

static gboolean resize_button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    double change = event->y - frame_resize_origin;
    GList *prev = NULL;
    GList *list = gtk_container_get_children(GTK_CONTAINER(editors_vbox));

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
        editor_t *preved = editors_editor_from_table((GtkWidget *)(prev->data));
        editor_t *nexted = editors_editor_from_table((GtkWidget *)(list->next->data));
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
        
        editors_adjust_size();
        gtk_widget_queue_draw(editors_vbox);
    }
    
    return TRUE;
}

static gboolean resize_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    gdk_window_set_cursor(gtk_widget_get_window(widget), gdk_cursor_new(GDK_DOUBLE_ARROW));
    return TRUE;
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
            g_signal_connect(G_OBJECT(resize_element), "button-press-event", G_CALLBACK(resize_button_press_callback), NULL);
            g_signal_connect(G_OBJECT(resize_element), "button-release-event", G_CALLBACK(resize_button_release_callback), NULL);
            g_signal_connect(G_OBJECT(resize_element), "expose-event", G_CALLBACK(resize_expose_event_callback), NULL);
            gtk_widget_add_events(resize_elements[i], GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        } else {
            resize_elements[i] = NULL;
        }

        gtk_container_add(GTK_CONTAINER(editors_vbox), editor->table);
        
        gtk_box_set_child_packing(GTK_BOX(editors_vbox), editor->table, empty ? TRUE : FALSE, empty ? TRUE : FALSE, 0, GTK_PACK_START);
        empty = 0;

        editor->allocated_vertical_space = editor_get_height_request(editor);

        editors_adjust_size();

        gtk_widget_show_all(editors_vbox);
        gtk_widget_queue_draw(editors_vbox);
    } else {
        editors_grow();
        editors_add(editor);
        return;
    }
}

editor_t *editors_new(buffer_t *buffer) {
    editor_t *e;

    e = new_editor(editors_window, buffer);

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

editor_t *editors_remove(editor_t *editor) {
    //TODO:
    // - if this is the last editor in the column show error message and exit
    // - otherwise remove this editor from the vbox
    // - remove the resizing element from the vbox
    // - remove the editor from the editors array
    // - remove the resizing element from the resizing elements array (if exists)
    // - destroy resizing element (if exists)
    // - call editor_free
    // - if this was the first editor in the vbox remove the resizing element of the next editor and make the next editor the first editor (extend + fill properties)
    // - return the very next editor in the vbox 
}
