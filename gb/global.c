#include "global.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void utf32_to_utf8(uint32_t code, char **r, int *cap, int *allocated) {
	int first_byte_pad, first_byte_mask, inc;

	if (code <= 0x7f) {
		inc = 0;
		first_byte_pad = 0x00;
		first_byte_mask = 0x7f;
	} else if (code <= 0x7ff) {
		inc = 1;
		first_byte_pad = 0xc0;
		first_byte_mask = 0x1f;
	} else if (code <= 0xffff) {
		inc = 2;
		first_byte_pad = 0xe0;
		first_byte_mask = 0x0f;
	} else if (code <= 0x1fffff) {
		inc = 3;
		first_byte_pad = 0xf8;
		first_byte_mask = 0x07;
	}

	if (*cap+inc >= *allocated) {
		*allocated *= 2;
		*r = realloc(*r, sizeof(char) * *allocated);
		alloc_assert(r);
	}

	for (int i = inc; i > 0; --i) {
		(*r)[*cap+i] = ((uint8_t)code & 0x3f) + 0x80;
		code >>= 6;
	}

	(*r)[*cap] = ((uint8_t)code & first_byte_mask) + first_byte_pad;

	*cap += inc + 1;

	return;
}

static char first_byte_result_to_mask[] = { 0xff, 0x3f, 0x1f, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00 };

static uint8_t utf8_first_byte_processing(uint8_t ch) {
	if (ch <= 127) return 0;

	if ((ch & 0xF0) == 0xF0) {
		if ((ch & 0x08) == 0x00) return 3;
		else return 8; // invalid sequence
	}

	if ((ch & 0xE0) == 0xE0) return 2;
	if ((ch & 0xC0) == 0xC0) return 1;
	if ((ch & 0x80) == 0x80) return 8; // invalid sequence

	return 8; // invalid sequence
}

uint32_t utf8_to_utf32(const char *text, int *src, int len, bool *valid) {
	uint32_t code;
	*valid = true;

	/* get next unicode codepoint in code, advance src */
	if ((uint8_t)text[*src] > 127) {
		uint8_t tail_size = utf8_first_byte_processing(text[*src]);

		if (tail_size >= 8) {
			code = (uint8_t)text[*src];
			++(*src);
			*valid = false;
			return code;
		}

		code = ((uint8_t)text[*src]) & first_byte_result_to_mask[tail_size];
		++(*src);

		/*printf("   Next char: %02x (%02x)\n", (uint8_t)text[src], (uint8_t)text[src] & 0xC0);*/

		int i = 0;
		for (; (((uint8_t)text[*src] & 0xC0) == 0x80) && (*src < len); ++(*src)) {
			code <<= 6;
			code += (text[*src] & 0x3F);
			++i;
		}

		if (i != tail_size) {
			*valid = false;
		}
	} else {
		code = text[*src];
		++(*src);
	}

	return code;
}

void utf8_remove_truncated_characters_at_end(char *text) {
	if (!text) return;

	int src = 0, len = strlen(text);

	for (; src < len; ) {
		bool valid = false;
		char *start = text + src;
		utf8_to_utf32(text, &src, len, &valid);
		if (!valid) {
			*start = '\0';
			return;
		}
	}
}

void alloc_assert(void *p) {
	if (!p) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
}

uint32_t *utf8_to_utf32_string(const char *text, int *dstlen) {
	int len = strlen(text);
	uint32_t *r = malloc(len*sizeof(uint32_t));
	alloc_assert(r);

	*dstlen = 0;
	for (int i = 0; i < len; ) {
		bool valid = true;
		r[(*dstlen)++] = utf8_to_utf32(text, &i, len, &valid);
	}

	return r;
}

int null_strcmp(const char *a, const char *b) {
	if (a == NULL) {
		if (b == NULL) return 0;
		if (strcmp(b, "") == 0) return 0;
		return -1;
	}

	if (b == NULL) {
		if (strcmp(a, "") == 0) return 0;
		return -1;
	}

	return strcmp(a, b);
}
