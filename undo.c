#include "undo.h"

#include <stdlib.h>

void undo_init(undo_t *undo) {
    undo->head = NULL;
}

void undo_free(undo_t *undo) {
    undo_node_t *cur;
    for(;;) {
        cur = undo_pop(undo);
        if (cur == NULL) break;
        undo_node_free(cur);
    } 
}

void undo_node_free(undo_node_t *node) {
    free(node->before_selection.text);
    free(node->after_selection.text);
    free(node);
}

void undo_push(undo_t *undo, undo_node_t *new_node) {
    new_node->prev = undo->head;
    new_node->next = NULL;
    undo->head = new_node;
}

undo_node_t *undo_pop(undo_t *undo) {
    undo_node_t *r = undo->head;

    if (r != NULL) {
        undo->head = r->prev;
        if (undo->head != NULL) {
            undo->head->next = NULL;
        }
    }

    return r;
}
