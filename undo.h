#ifndef __UNDO_H__
#define __UNDO_H__

#include <stdbool.h>

#include <time.h>

#define TYPING_FUSION_INTERVAL 2

typedef struct _selection_t {
	int start, end;
	char *text;
} selection_t;

typedef struct _undo_node_t {
	selection_t before_selection;
	selection_t after_selection;

	struct _undo_node_t *prev;
	struct _undo_node_t *next;

	char *tag;

	bool fake;
	time_t time;
} undo_node_t;

typedef struct _undo_t {
	/* head of the undo stack, in other words, the very last undo node added to the list */
	undo_node_t *head;

	bool please_fuse;
} undo_t;

void undo_init(undo_t *undo);
void undo_free(undo_t *undo);

/* adds node to undo list */
void undo_push(undo_t *undo, undo_node_t *new_node);

/* returns a node from the undo list, moves the undo list head backwards */
undo_node_t *undo_pop(undo_t *undo);
/* returns a node from the undo list before the head */
undo_node_t *undo_redo_pop(undo_t *undo);

// peeks at first node in the undo list
undo_node_t *undo_peek(undo_t *undo);

#endif
