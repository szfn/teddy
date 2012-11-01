#ifndef __ACMACS_GLOBAL_H__
#define __ACMACS_GLOBAL_H__

#include <stdint.h>
#include <stdbool.h>

void utf32_to_utf8(uint32_t code, char **r, int *cap, int *allocated);
uint32_t utf8_to_utf32(const char *text, int *src, int len, bool *valid);
uint32_t *utf8_to_utf32_string(const char *text, int *dstlen);
void utf8_remove_truncated_characters_at_end(char *text);

void alloc_assert(void *p);

#endif