#include "editors.h"

static editor_t **editors;
static int editors_allocated;
static GtkWidget *editors_window;
static GtkWidget *editors_vbox;
static int after_show = 0;

void editors_init(GtkWidget *window) {
    int i;
    
    editors_allocated = 10;
    editors = malloc(sizeof(editor_t *) * editors_allocated);
    if (!editors) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < editors_allocated; ++i) {
        editors[i] = NULL;
    }

    editors_vbox = gtk_vbox_new(FALSE, 2);
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
}

static void editors_grow(void) {
    int i;
    
    editors = realloc(editors, sizeof(editor_t *) * editors_allocated * 2);
    if (!editors) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = editors_allocated; i < editors_allocated * 2; ++i) {
        editors[i] = NULL;
    }
    editors_allocated *= 2;
}

void editors_add(editor_t *editor) {
    int i;
    
    for (i = 0; i < editors_allocated; ++i) {
        if (editors[i] == NULL) break;
    }

    if (i < editors_allocated) {
        editors[i] = editor;
        gtk_container_add(GTK_CONTAINER(editors_vbox), editor->table);
    } else {
        editors_grow();
        editors_add(editor);
    }

    if (after_show) {
        editor_post_show_setup(editor);
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
        if (editors[i] == NULL) continue;
        editor_post_show_setup(editors[i]);
    }
    after_show = 1;
}
