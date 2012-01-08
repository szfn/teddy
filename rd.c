#include "rd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <stdbool.h>

#include "baux.h"
#include "lexy.h"

struct entry {
	char *name;
	ino_t inode;
	unsigned char type;
};

struct entries {
	struct entry *v;
	size_t cur;
	size_t allocated;
};

static void entry_init(struct entries *es, size_t initial_allocation) {
	es->allocated = initial_allocation;
	es->v = malloc(sizeof(struct entry) * initial_allocation);
	if (es->v == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
	es->cur = 0;
}

static void entry_free(struct entries *es) {
	for (int i = 0; i < es->cur; ++i) {
		free(es->v[i].name);
	}
	free(es->v);
}

static void entry_append(struct entries *es, const char *name, ino_t inode, unsigned char d_type) {
	if (es->cur >= es->allocated) {
		es->allocated *= 2;
		es->v = realloc(es->v, sizeof(struct entry) * es->allocated);
		if (es->v == NULL) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}
	}

	es->v[es->cur].name = strdup(name);
	if (es->v[es->cur].name == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
	es->v[es->cur].inode = inode;
	es->v[es->cur].type = d_type;
	++(es->cur);
}

unsigned char order_by_d_type[0xff+1];

static int entry_cmp(const void *pa, const void *pb) {
	struct entry *a = (struct entry *)pa;
	struct entry *b = (struct entry *)pb;

	int d = order_by_d_type[a->type] - order_by_d_type[b->type];
	if (d != 0) return d;

	return strcmp(a->name, b->name);
}

static void entry_sort(struct entries *es) {
	qsort(es->v, es->cur, sizeof(struct entry), entry_cmp);
}

char type_identifier[0xff+1];

static void rdapp(buffer_t *buffer, const char *c, uint8_t color) {
	buffer->default_color = color;
	buffer_append(buffer, c, strlen(c), false);
	buffer->default_color = L_NOTHING;
}

void rd(DIR *dir, buffer_t *buffer) {
	struct entries es;
	entry_init(&es, 100);

	struct dirent entry;
	struct dirent *result;

	rdapp(buffer, buffer->path, L_KEYWORD);
	rdapp(buffer, ":\n", L_KEYWORD);

	for (;;) {
		int r = readdir_r(dir, &entry, &result);
		if (r != 0) {
			rdapp(buffer, "Error reading directory\n", L_COMMENT);
			break;
		}
		if (result == NULL) {
			// end of stream
			break;
		}
		if (entry.d_name[0] == '.') continue;

		entry_append(&es, entry.d_name, entry.d_ino, entry.d_type);
	}

	entry_sort(&es);

	char cur[255] = "";
	bool first = true;

	for (int i = 0; i < es.cur; ++i) {
		//printf("Comparing [%s] [%s]\n", cur, es.v[i].name);
		if ((cur[0] == '\0') || strncmp(cur, es.v[i].name, strlen(cur)) != 0) {
			if (!first) rdapp(buffer, "\n", L_NOTHING);
			char *dot = strchr(es.v[i].name, '.');
			if (dot != NULL) {
				int len = dot - es.v[i].name;
				if (len < 254) {
					strncpy(cur, es.v[i].name, len+1);
					cur[len+1] = '\0';
				} else {
					//printf("len: %d\n", len);
					cur[0] = '\0';
				}
			} else {
				//printf("No dot found\n");
				cur[0] = '\0';
			}
		} else {
			rdapp(buffer, "\t", L_NOTHING);
		}

		first = false;

		char id = type_identifier[es.v[i].type];
		rdapp(buffer, es.v[i].name, (es.v[i].type == DT_DIR) ? L_STRING : L_NOTHING);
		if (id != ' ') {
			char z[2] = { 0, 0 };
			z[0] = id;
			rdapp(buffer, z, L_COMMENT);
		}
	}

	rdapp(buffer, "\n", L_NOTHING);

	entry_free(&es);
}

void rd_init(void) {
	for (int i = 0; i <= 0xff; ++i) {
		order_by_d_type[i] = 0xff;
		type_identifier[i] = ' ';
	}

	order_by_d_type[DT_BLK] = 50; type_identifier[DT_BLK] = '%';
	order_by_d_type[DT_CHR] = 60; type_identifier[DT_CHR] = '^';
	order_by_d_type[DT_DIR] = 10; type_identifier[DT_DIR] = '/';
	order_by_d_type[DT_FIFO] = 30; type_identifier[DT_FIFO] = '|';
	order_by_d_type[DT_LNK] = 20; type_identifier[DT_LNK] = '@';
	order_by_d_type[DT_REG] = 20; type_identifier[DT_REG] = ' ';
	order_by_d_type[DT_SOCK] = 40; type_identifier[DT_SOCK] = '=';
	order_by_d_type[DT_UNKNOWN] = 70; type_identifier[DT_UNKNOWN] = '?';
}
