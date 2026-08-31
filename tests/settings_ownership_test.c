#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/border.h"

static TABLE_HASH_FUNC(hash_string) {
  unsigned long hash = 5381;
  char c;
  while ((c = *((char*)key++))) hash = ((hash << 5) + hash) + c;
  return hash;
}

static TABLE_COMPARE_FUNC(compare_string) {
  return strcmp(key_a, key_b) == 0;
}

int main(void) {
  struct settings original = {};
  settings_init_filter_tables(&original, 8, hash_string, compare_string);
  char safari[] = "Safari";
  _table_add(&original.blacklist, safari, sizeof(safari), (void*)true);

  struct settings clone;
  settings_clone(&clone, &original);
  assert(clone.owns_filter_tables);
  assert(clone.blacklist.buckets != original.blacklist.buckets);
  assert(table_find(&clone.blacklist, safari));

  struct settings snapshot;
  settings_snapshot(&snapshot, &clone);
  assert(!snapshot.owns_filter_tables);
  assert(snapshot.blacklist.buckets == clone.blacklist.buckets);

  settings_take_filter_ownership(&snapshot);
  assert(snapshot.owns_filter_tables);
  assert(snapshot.blacklist.buckets != clone.blacklist.buckets);
  table_clear(&snapshot.blacklist);
  assert(!table_find(&snapshot.blacklist, safari));
  assert(table_find(&clone.blacklist, safari));

  struct settings replacement = {};
  settings_replace(&replacement, &clone);
  settings_destroy(&clone);
  assert(table_find(&replacement.blacklist, safari));

  struct settings moved = {};
  settings_move(&moved, &replacement);
  assert(moved.owns_filter_tables);
  assert(!replacement.owns_filter_tables);
  assert(!replacement.blacklist.buckets);
  assert(table_find(&moved.blacklist, safari));

  settings_destroy(&snapshot);
  settings_destroy(&moved);
  settings_destroy(&original);
  puts("settings filter ownership: ok");
  return 0;
}
