#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "global.h"

#ifndef SLOP
#define SLOP 8
#endif

#define MAX(a, b) (((a) >= (b)) ? (a) : (b))
#define MIN(a, b) (((a) <= (b)) ? (a) : (b))

typedef struct _my_glyph_info_t {
	uint32_t code;
	uint8_t color;

	double kerning_correction;
	double x_advance;

	unsigned long glyph_index;
	uint8_t fontidx;
	double x;
	double y;
} my_glyph_info_t;

#define GBDEF my_glyph_info_t *buf; size_t size; int cursor; int mark; int gap; size_t gapsz;

#ifndef GBENCL
#define GBENCL struct gb
struct gb {
	GBDEF
};
#define DEFMAIN
#endif

void gb_init(GBENCL *encl) {
	encl->buf = malloc(sizeof(my_glyph_info_t) * SLOP);
	alloc_assert(encl->buf);
	encl->size = SLOP;
	encl->cursor = -1;
	encl->mark = -1;
	encl->gap = 0;
	encl->gapsz = SLOP;
}

static int phisical(GBENCL *encl, int point) {
	return (point < encl->gap) ? point : point + encl->gapsz;
}

my_glyph_info_t *bat(GBENCL *encl, int point) {
	if (point < 0) return NULL;
	int pp = (point < encl->gap) ? point : point + encl->gapsz;
	return (pp < encl->size) ? encl->buf + pp : NULL;
}

void gb_debug_print(GBENCL *encl) {
	int pp_cursor = phisical(encl, encl->cursor);
	int pp_mark = phisical(encl, encl->mark);

	printf("(size: %zd / gapsz: %zd / cursor %d(%d) / gap (%d)) ", encl->size, encl->gapsz, encl->cursor, pp_cursor, encl->gap);

	int gapsz = 0;

	for (int i = 0; i < encl->size; ++i) {
		//printf("%d ", i);

		if (i == encl->gap) {
			printf("<gap: %zd> ", encl->gapsz);
			gapsz = encl->gapsz;
		}

		if (gapsz <= 0) {
			char c = encl->buf[i].code;
			if ((c >= 'A') && (c <= 'z'))
				printf("[%c] ", c);
			else
				printf("[%02x] ", c);
		} else {
			--gapsz;
			if (gapsz == 0) {
				printf("</gap> ");
			}
		}

		if (i == pp_cursor) {
			printf("<%d cursor/> ", i);
		}
		if (i == pp_mark) {
			printf("<%d mark/> ", i);
		}

		//printf("\n");
	}
	if (gapsz > 0) {
		printf(" (remaining gapsz: %d)\n", gapsz);
	} else {
		printf("\n");
	}
}

char *gb_debug_str(GBENCL *encl) {
	char *r = malloc(sizeof(char) * (encl->size-encl->gapsz + 1));
	alloc_assert(r);
	for (int i = 0; i < encl->size-encl->gapsz; ++i) {
		char c = bat(encl, i)->code;
		r[i] = ((c >= 'A') && (c <= 'z')) ? c : '?';
	}
	r[encl->size-encl->gapsz] = '\0';
	return r;
}

static int movegap(GBENCL *encl, int point) {
	int pp = phisical(encl, point);
	if (pp < encl->gap) {
		printf("slide forward %d -> %zd (size: %d) (%p)\n", pp, pp + encl->gapsz, encl->gap - pp, encl->buf);
		memmove(encl->buf + pp + encl->gapsz, encl->buf + pp, sizeof(my_glyph_info_t) * (encl->gap - pp));
		encl->gap = pp;
		printf("after slide: ");
		gb_debug_print(encl);
	} else if (pp > encl->gap) {
		printf("slide backward %zd -> %d (size: %zd) (%p)\n", encl->gap + encl->gapsz, encl->gap, pp - encl->gap - encl->gapsz, encl->buf);
		memmove(encl->buf + encl->gap, encl->buf + encl->gap + encl->gapsz, sizeof(my_glyph_info_t) * (pp - encl->gap - encl->gapsz));
		encl->gap = pp - encl->gapsz;
	}

	printf("encl->size = %zd\n", encl->size);

}

static void regap(GBENCL *encl) {
	printf("before regap: ");
	gb_debug_print(encl);

	my_glyph_info_t *newbuf = malloc(sizeof(my_glyph_info_t) * (encl->size+SLOP));
	alloc_assert(newbuf);

	memmove(newbuf, encl->buf, sizeof(my_glyph_info_t) * encl->gap);
	memmove(newbuf+encl->gap+SLOP, encl->buf + encl->gap, sizeof(my_glyph_info_t) * (encl->size - encl->gap));

	encl->gapsz = SLOP;
	encl->size += SLOP;

	free(encl->buf);
	encl->buf = newbuf;

	printf("after regap: ");
	gb_debug_print(encl);
}

void buffer_replace_selection(GBENCL *encl, char *text) {
	//TODO: freeze selection before

	// there is a mark, delete
	if (encl->mark >= 0) {
		int region_size = MAX(encl->mark, encl->cursor) - MIN(encl->mark, encl->cursor);
		movegap(encl, MIN(encl->mark, encl->cursor)+1);
		encl->gapsz += region_size;
		encl->cursor = MIN(encl->mark, encl->cursor);
		encl->mark = -1;
	} else {
		printf("Calling movegap: %d\n", encl->cursor+1);
		movegap(encl, encl->cursor+1);
	}

	int len = strlen(text);
	for (int i = 0; i < len; ) {
		if (encl->gapsz <= 0) regap(encl);

		bool valid = false;
		uint32_t code = utf8_to_utf32(text, &i, len, &valid);

		encl->buf[encl->gap].code = code;

		//TODO: adjust other parameters

		++(encl->cursor);
		++(encl->gap);
		--(encl->gapsz);
	}

	//TODO: adjust kerning of next character

	//TODO: freeze selection after
}

void gb_assert(GBENCL *encl, const char *target, int cursor, int gap, int gapsz) {
	if (encl->gap != gap) {
		printf("Wrong gap position %d, expected %d\n", encl->gap, gap);
		exit(EXIT_FAILURE);
	}
	if (encl->gapsz != gapsz) {
		printf("Wrong gap size %zd, expected %d\n", encl->gapsz, gapsz);
		exit(EXIT_FAILURE);
	}
	if (encl->cursor != cursor) {
		printf("Wrong cursor position %d, expected %d\n", encl->cursor, cursor);
		exit(EXIT_FAILURE);
	}
	char *r = gb_debug_str(encl);
	if (strcmp(r, target) != 0) {
		printf("Output discrepancy <%s>, expected: <%s>\n", r, target);
		exit(EXIT_FAILURE);
	}
	free(r);
}

#ifdef DEFMAIN
int main(void) {
	struct gb encl;
	gb_init(&encl);
	gb_debug_print(&encl);

	buffer_replace_selection(&encl, "abcdefghijklmnopqrstuvwxyz");

	printf("after initial insertion: ");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcdefghijklmnopqrstuvwxyz", 25, 26, 6);

	printf("\n==== mid insertion ====\n");
	encl.cursor = 12;
	buffer_replace_selection(&encl, "A");

	printf("after mid insertion: ");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcdefghijklmAnopqrstuvwxyz", 13, 14, 5);

	printf("\n==== consuming gap ====\n");
	buffer_replace_selection(&encl, "BCDEF");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcdefghijklmABCDEFnopqrstuvwxyz", 18, 19, 0);

	printf("\n==== gapless insertion ====\n");
	encl.cursor = 8;
	buffer_replace_selection(&encl, "GH");
	printf("after gapless insertion: ");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcdefghiGHjklmABCDEFnopqrstuvwxyz",10, 11, 6);

	printf("\n==== replacement after gap ====\n");
	encl.mark = 13;
	encl.cursor = 15;
	buffer_replace_selection(&encl, "I");
	printf("after replacement after gap: ");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcdefghiGHjklIBCDEFnopqrstuvwxyz", 14, 15, 7);

	printf("\n==== replacement before gap ====\n");
	encl.mark = 2;
	encl.cursor = 6;
	buffer_replace_selection(&encl, "JK");
	printf("after replacement before gap: ");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcJKhiGHjklIBCDEFnopqrstuvwxyz", 4, 5, 9);


	// abcJK[]hiGHjklIBCDEFnopqrstuvwxyz

	printf("\n==== replacement containing gap ====\n");
	encl.cursor = 3;
	encl.mark = 7;
	buffer_replace_selection(&encl, "LMNO");
	printf("after replacement midgap: ");
	gb_debug_print(&encl);
	gb_assert(&encl, "abcJLMNOHjklIBCDEFnopqrstuvwxyz", 7, 8, 9);

	printf("\n\n");
}
#endif
