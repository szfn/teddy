#ifndef __TAGS__
#define __TAGS__

#include <stdbool.h>

struct tag_entry {
	char *tag;
	char *path;
	char *search;
	int lineno;
};

extern struct tag_entry *tag_entries;
extern int tag_entries_cap;

void tags_init(void);
void tags_load(char *wd);
bool tags_loaded(void);

#endif
