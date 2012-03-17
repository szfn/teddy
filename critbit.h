#ifndef CRITBIT_H_
#define CRITBIT_H_

typedef struct {
  void *root;
} critbit0_tree;

int critbit0_contains(critbit0_tree *t, const char *u);
int critbit0_insert(critbit0_tree *t, const char *u);
int critbit0_delete(critbit0_tree *t, const char *u);
void critbit0_clear(critbit0_tree *t);
int critbit0_allprefixed(critbit0_tree *t, const char *prefix, int (*handle) (const char *, void *), void *arg);
char *critbit0_common_suffix_for_prefix(critbit0_tree *t, const char *u);

#endif  // CRITBIT_H_
