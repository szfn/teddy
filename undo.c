#include "undo.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

//#define UNDO_DEBUGGING

static void undo_node_free(undo_node_t *node) {
	free(node->before_selection.text);
	free(node->after_selection.text);
	if (node->tag != NULL) free(node->tag);
	free(node);
}

void undo_init(undo_t *undo) {
	undo->head = malloc(sizeof(undo_node_t));
	undo->head->tag = NULL;
	undo->head->fake = true;
	undo->head->prev = NULL;
	undo->head->next = NULL;
	undo->please_fuse = false;
}

void undo_free(undo_t *undo) {
	undo_node_t *cur;
	for(;;) {
		cur = undo_pop(undo);
		if (cur == NULL) {
			break;
		}
		undo_node_free(cur);
	}
}

static void debug_print_selection(selection_t *selection) {
	printf("   %d:%d,%d:%d [[%s]]\n", selection->start.lineno, selection->start.glyph, selection->end.lineno, selection->end.glyph, selection->text);
}

#ifdef UNDO_DEBUGGING
static void debug_print_undo(undo_node_t *node) __attribute__ ((unused));
static void debug_print_undo(undo_node_t *node) {
	if (node == NULL) {
		printf("   (null)\n");
	} else {
		if (node->fake) {
			printf("   fake %d\n", node->fake);
		} else {
			debug_print_selection(&(node->before_selection));
			debug_print_selection(&(node->after_selection));
		}
	}
}
#endif

static bool selection_is_empty(selection_t *selection) {
	return points_equal(&(selection->start), &(selection->end)) && (selection->text[0] == '\0');
}

static bool selections_are_adjacent(selection_t *a, selection_t *b) {
	return points_equal(&(a->end), &(b->start));
}

static int selection_len(selection_t *selection) {
	if (selection->start.lineno != selection->end.lineno) return -1;
	int r = selection->end.glyph - selection->start.glyph;
	return (r < 0) ? -r : r;
}

static void selections_cat(selection_t *dst, selection_t *src) {
	dst->end.lineno = src->end.lineno;
	dst->end.glyph = src->end.glyph;

	char *newtext = malloc(strlen(src->text) + strlen(dst->text) + 1);

	strcpy(newtext, dst->text);
	strcat(newtext, src->text);

	free(dst->text);
	dst->text = newtext;
}

static void undo_drop_redo_info(undo_t *undo) {
	if (undo->head == NULL) return;

	undo_node_t *node = undo->head->next;
	while (node != NULL) {
		undo_node_t *next = node->next;
		undo_node_free(node);
		node = next;
	}
}

void undo_push(undo_t *undo, undo_node_t *new_node) {
	time_t now = time(NULL);

	undo_drop_redo_info(undo);

	new_node->fake = false;

	// when appropriate we fuse the new undo node with the last one so you don't have to undo typing one character at a time
	if (undo->please_fuse
	  && (undo->head != NULL)
	  && !undo->head->fake
	  && selections_are_adjacent(&(undo->head->after_selection), &(new_node->after_selection))) {
	    //debug_print_undo(undo->head);
	    //debug_print_undo(new_node);
	    selections_cat(&(undo->head->before_selection), &(new_node->before_selection));
		selections_cat(&(undo->head->after_selection), &(new_node->after_selection));
		undo->head->time = now;
		undo_node_free(new_node);
		//debug_print_undo(undo->head);
	} else if ((undo->head != NULL)
	  && !undo->head->fake
      && selection_is_empty(&(undo->head->before_selection))
      && selection_is_empty(&(new_node->before_selection))
      && (selection_len(&(new_node->after_selection)) == 1)
      && (new_node->after_selection.text[0] != ' ')
      && selections_are_adjacent(&(undo->head->after_selection), &(new_node->after_selection))
      && ((now - undo->head->time) < TYPING_FUSION_INTERVAL)) {
		selections_cat(&(undo->head->after_selection), &(new_node->after_selection));
		undo->head->time = now;
		undo_node_free(new_node);
	} else {
		// normal undo node append code
		if (undo->head != NULL) undo->head->next = new_node;
		new_node->prev = undo->head;
		new_node->next = NULL;
		new_node->time = now;
		undo->head = new_node;
	}

#ifdef UNDO_DEBUGGING
	printf("PUSHING\n");
	debug_print_undo(undo->head);
#endif

	undo->please_fuse = false;
}

undo_node_t *undo_pop(undo_t *undo) {
	undo_node_t *r = undo->head;
	if (r->fake) return NULL;

	if (r != NULL) {
		undo->head = r->prev;
	}

#ifdef UNDO_DEBUGGING
	printf("POPPING\n");
	debug_print_undo(r);
#endif

	return r;
}

undo_node_t *undo_redo_pop(undo_t *undo) {
#ifdef UNDO_DEBUGGING
	printf("Attempting to redo pop\n");
	debug_print_undo(undo->head);
	printf("%p %p\n", undo->head, undo->head->next);
#endif
	if (undo->head == NULL) return NULL;
	if (undo->head->next == NULL) return NULL;

	undo->head = undo->head->next;

#ifdef UNDO_DEBUGGING
	printf("REDO POPPING\n");
	debug_print_undo(undo->head);
#endif

	return undo->head;
}

undo_node_t *undo_peek(undo_t *undo) {
	return undo->head;
}

