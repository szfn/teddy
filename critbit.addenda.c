#include <stdio.h>

char *strdup(const char *s);

char *critbit0_common_suffix_for_prefix(critbit0_tree *t, const char *u) {
	const uint8 *ubytes = (void *)u;
	const size_t ulen = strlen(u);
	uint8_t *p = t->root;
	int64_t first_differing_byte = -1;
	if (!p) return strdup("");

	while (1 & (intptr_t)p) {
		critbit0_node *q = (void *)(p - 1);

		uint8 c = 0;
		if (q->byte < ulen) c = ubytes[q->byte];
		else {
			if (first_differing_byte < 0) first_differing_byte = q->byte;
		}

		const int direction = (1 + (q->otherbits | c)) >> 8;

		p = q->child[direction];
	}

	if (strncmp((const char *)p, u, ulen) != 0) return NULL;

	if (first_differing_byte < 0) first_differing_byte = strlen((const char *)p) + 1;

	return strndup(((const char *)p)+ulen, first_differing_byte-ulen);
}
