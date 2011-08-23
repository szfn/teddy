#ifndef __UNDO_H__
#define __UNDO_H__

typedef struct _point_t {
    int lineno;
    int glyph;
} point_t;

typedef struct _selection_t {
    point_t start;
    point_t end;
    char *text;
} selection_t;

typedef struct _undo_node_t {
    selection_t before_selection;
    selection_t after_selection;

    struct _undo_node_t *prev;
    struct _undo_node_t *next;
} undo_node_t;

typedef struct _undo_t {
    /* head of the undo stack, in other words, the very last undo node added to the list */    
    undo_node_t *head;
} undo_t;

void undo_init(undo_t *undo);
void undo_free(undo_t *undo);

void undo_node_free(undo_node_t *node);

/* adds node to undo list */
void undo_push(undo_t *undo, undo_node_t *new_node);

/* removes node from undo list and returns it, you are responsible for free'ing it */
undo_node_t *undo_pop(undo_t *undo);

#endif
