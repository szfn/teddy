#include "buffers.h"

#include <stdlib.h>
#include <stdio.h>

buffer_t **buffers;
int buffers_allocated;

void buffers_init(void) {
    int i;
    buffers_allocated = 10;
    buffers = malloc(sizeof(buffer_t *) * buffers_allocated);

    for (i = 0; i < buffers_allocated; ++i) {
        buffers[i] = NULL;
    }

    if (!buffers) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
}


static void buffers_grow() {
    int i;
    buffers = realloc(buffers, sizeof(buffer_t *) * buffers_allocated * 2);
    if (!buffers) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }
    for (i = buffers_allocated; i < buffers_allocated * 2; ++i) {
        buffers[i] = NULL;
    }
    buffers_allocated *= 2;
}

void buffers_add(buffer_t *b) {
    int i;
    for (i = 0; i < buffers_allocated; ++i) {
        if (buffers[i] == NULL) {
            buffers[i] = b;
            break;
        }
    }

    if (i >= buffers_allocated) {
        buffers_grow();
        buffers_add(b);
    }
}

void buffers_free(void) {
    int i;
    for (i = 0; i < buffers_allocated; ++i) {
        if (buffers[i] != NULL) {
            buffer_free(buffers[i]);
            buffers[i] = NULL;
        }
    }
}
