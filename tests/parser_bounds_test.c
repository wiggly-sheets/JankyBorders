#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/parse.c"

static TABLE_HASH_FUNC(hash_string) {
  unsigned long hash = 5381;
  char c;
  while ((c = *((char*)key++))) hash = ((hash << 5) + hash) + c;
  return hash;
}

static TABLE_COMPARE_FUNC(compare_string) {
  return strcmp(key_a, key_b) == 0;
}

static uint32_t parse_one(struct settings* settings, char* argument) {
  return parse_settings(settings, 1, &argument);
}

int main(void) {
  struct settings settings = {
    .border_order = BORDER_ORDER_BELOW,
    .border_style = BORDER_STYLE_ROUND,
    .border_width = 4.0f,
    .inner_border_width = 4.0f,
    .double_border_gap = 1.0f,
    .shimmer_duration = 3.0f,
    .shimmer_fps = 30.0f,
    .animation_duration = 0.25f,
  };
  settings_init_filter_tables(&settings, 8, hash_string, compare_string);

  char order_above[] = "order=above";
  assert(parse_one(&settings, order_above) == BORDER_UPDATE_MASK_ALL);
  assert(settings.border_order == BORDER_ORDER_ABOVE);
  char invalid_order[] = "order=aardvark";
  assert(parse_one(&settings, invalid_order) == 0);
  assert(settings.border_order == BORDER_ORDER_ABOVE);
  char trailing_order[] = "order=below-now";
  assert(parse_one(&settings, trailing_order) == 0);
  assert(settings.border_order == BORDER_ORDER_ABOVE);

  char duration[] = "animation_duration=60";
  assert(parse_one(&settings, duration) == BORDER_UPDATE_MASK_ANIMATION);
  char duration_high[] = "animation_duration=60.1";
  assert(parse_one(&settings, duration_high) == 0);
  char duration_low[] = "animation_duration=0.001";
  assert(parse_one(&settings, duration_low) == 0);
  char duration_trailing[] = "animation_duration=1second";
  assert(parse_one(&settings, duration_trailing) == 0);
  assert(settings.animation_duration == 60.0f);

  char shimmer_duration[] = "shimmer_duration=3600";
  assert(parse_one(&settings, shimmer_duration) == BORDER_UPDATE_MASK_ALL);
  char shimmer_tiny[] = "shimmer_duration=1e-45";
  assert(parse_one(&settings, shimmer_tiny) == 0);
  char shimmer_trailing[] = "shimmer_duration=3seconds";
  assert(parse_one(&settings, shimmer_trailing) == 0);
  assert(settings.shimmer_duration == 3600.0f);

  char shimmer_fps[] = "shimmer_fps=120";
  assert(parse_one(&settings, shimmer_fps) == BORDER_UPDATE_MASK_ALL);
  char shimmer_fps_high[] = "shimmer_fps=121";
  assert(parse_one(&settings, shimmer_fps_high) == 0);
  char shimmer_fps_trailing[] = "shimmer_fps=30fps";
  assert(parse_one(&settings, shimmer_fps_trailing) == 0);
  assert(settings.shimmer_fps == 120.0f);

  char width[] = "width=256";
  assert(parse_one(&settings, width) == BORDER_UPDATE_MASK_ALL);
  char width_high[] = "width=257";
  assert(parse_one(&settings, width_high) == 0);
  char width_tiny[] = "width=0.01";
  assert(parse_one(&settings, width_tiny) == 0);
  char double_width_high[] = "width=double(4,257)";
  assert(parse_one(&settings, double_width_high) == 0);
  assert(settings.border_width == 256.0f);

  char gap[] = "double_gap=256";
  assert(parse_one(&settings, gap) == BORDER_UPDATE_MASK_ALL);
  char gap_high[] = "double_gap=257";
  assert(parse_one(&settings, gap_high) == 0);
  char gap_trailing[] = "double_gap=2px";
  assert(parse_one(&settings, gap_trailing) == 0);
  assert(settings.double_border_gap == 256.0f);

  char invalid_style[] = "style=roundish";
  assert(parse_one(&settings, invalid_style) == 0);
  assert(settings.border_style == BORDER_STYLE_ROUND);

  char apply_to[] = "apply-to=4294967295";
  assert(parse_one(&settings, apply_to) == BORDER_UPDATE_MASK_SETTING);
  assert(settings.apply_to == UINT32_MAX);
  char apply_to_overflow[] = "apply-to=4294967296";
  assert(parse_one(&settings, apply_to_overflow) == 0);
  char apply_to_trailing[] = "apply-to=42window";
  assert(parse_one(&settings, apply_to_trailing) == 0);
  char apply_to_zero[] = "apply-to=0";
  assert(parse_one(&settings, apply_to_zero) == 0);
  assert(settings.apply_to == UINT32_MAX);

  char blacklist[] = "blacklist=Safari";
  assert(parse_one(&settings, blacklist) == BORDER_UPDATE_MASK_RECREATE_ALL);
  struct settings snapshot;
  settings_snapshot(&snapshot, &settings);
  char replacement[] = "blacklist=kitty";
  assert(parse_one(&snapshot, replacement) == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(snapshot.owns_filter_tables);
  assert(table_find(&settings.blacklist, "Safari"));
  assert(!table_find(&settings.blacklist, "kitty"));
  assert(table_find(&snapshot.blacklist, "kitty"));

  char oversized[SETTINGS_MAX_TOKEN_LENGTH + 2];
  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = '\0';
  assert(parse_one(&settings, oversized) == 0);

  settings_destroy(&snapshot);
  settings_destroy(&settings);
  puts("strict parser bounds: ok");
  return 0;
}
