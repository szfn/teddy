#include <string>
#include <cstring>
#include <stdlib.h>
#include <set>

extern "C" {
#include "critbit.h"
}

using namespace std;

static void
test_contains() {
  critbit0_tree tree = {0};

  static const char *elems[] = {"a", "aa", "b", "bb", "ab", "ba", "aba", "bab", NULL};

  for (unsigned i = 0; elems[i]; ++i) critbit0_insert(&tree, elems[i]);

  for (unsigned i = 0; elems[i]; ++i) {
    if (!critbit0_contains(&tree, elems[i])) abort();
  }

  critbit0_clear(&tree);
}

static void
test_delete() {
  critbit0_tree tree = {0};

  static const char *elems[] = {"a", "aa", "b", "bb", "ab", "ba", "aba", "bab", NULL};

  for (unsigned i = 1; elems[i]; ++i) {
    critbit0_clear(&tree);

    for (unsigned j = 0; j < i; ++j) critbit0_insert(&tree, elems[j]);
    for (unsigned j = 0; j < i; ++j) {
      if (!critbit0_contains(&tree, elems[j])) abort();
    }
    for (unsigned j = 0; j < i; ++j) {
      if (1 != critbit0_delete(&tree, elems[j])) abort();
    }
    for (unsigned j = 0; j < i; ++j) {
      if (critbit0_contains(&tree, elems[j])) abort();
    }
  }

  critbit0_clear(&tree);
}

static int
allprefixed_cb(const char *elem, void *arg) {
  set<string> *a = (set<string> *) arg;
  a->insert(elem);

  return 1;
}

static void
test_allprefixed() {
  critbit0_tree tree = {0};

  static const char *elems[] = {"a", "aa", "aaz", "abz", "bba", "bbc", "bbd", NULL};

  for (unsigned i = 0; elems[i]; ++i) critbit0_insert(&tree, elems[i]);

  set<string> a;

  critbit0_allprefixed(&tree, "a", allprefixed_cb, &a);
  if (a.size() != 4 ||
      a.find("a") == a.end() ||
      a.find("aa") == a.end() ||
      a.find("aaz") == a.end() ||
      a.find("abz") == a.end()) {
    abort();
  }
  a.clear();

  critbit0_allprefixed(&tree, "aa", allprefixed_cb, &a);
  if (a.size() != 2 ||
      a.find("aa") == a.end() ||
      a.find("aaz") == a.end()) {
    abort();
  }
  a.clear();

  critbit0_clear(&tree);
}

#include <stdio.h>

static void
test_common_suffix_for_prefix() {
critbit0_tree tree = {0};

  static const char *elems[] = {"abb_nab_vz", "abb_nab_lb", "abb_nab_ft", "abb_nab_pr", "abt_nab_lz", "abz_nab_rd", "mabb_nab_gz", NULL};

  for (unsigned i = 0; elems[i]; ++i) critbit0_insert(&tree, elems[i]);

  //Note: leaking memory here

  if (strcmp(critbit0_common_suffix_for_prefix(&tree, "a"), "b") != 0) abort();
  if (strcmp(critbit0_common_suffix_for_prefix(&tree, "ab"), "") != 0) abort();
  if (strcmp(critbit0_common_suffix_for_prefix(&tree, "abb"), "_nab_") != 0) abort();
  if (strcmp(critbit0_common_suffix_for_prefix(&tree, "abb_nab_lz"), "") != 0) abort();
}

int
main() {
  test_contains();
  test_delete();
  test_allprefixed();
  test_common_suffix_for_prefix();

  return 0;
}
