#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/animation.c"

struct settings g_settings;
struct table g_windows;

struct settings* border_get_settings(struct border* border) {
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

static int animating_update_count;
static int final_update_count;

void border_update_animating(struct border* border, float progress) {
  (void)border;
  assert(progress == 1.0f);
  animating_update_count++;
}

void border_update(struct border* border, bool try_async) {
  (void)border;
  (void)try_async;
  final_update_count++;
}

int main(void) {
  assert(animation_ease(ANIMATION_EASING_LINEAR, 0.25f) == 0.25f);
  assert(animation_ease(ANIMATION_EASING_EASE_IN_EXPO, 0.0f) == 0.0f);
  assert(animation_ease(ANIMATION_EASING_EASE_OUT_EXPO, 1.0f) == 1.0f);
  assert(fabsf(animation_ease(ANIMATION_EASING_EASE_IN_OUT_EXPO, 0.5f)
               - 0.5f) < 0.0001f);

  struct border border = {
    .animating = true,
    .anim_mode = ANIM_SLIDE | ANIM_PULSE,
    .anim_start = CACurrentMediaTime() - 1.0,
    .anim_duration = 0.01f,
    .anim_origin_override = true,
  };
  struct bucket bucket = {
    .value = &border,
  };
  struct bucket* buckets[] = { &bucket };
  g_windows.buckets = buckets;
  g_windows.capacity = 1;
  animation_tick_callback(NULL, NULL);

  assert(!border.animating);
  assert(border.anim_mode == 0);
  assert(!border.anim_origin_override);
  assert(animating_update_count == 0);
  assert(final_update_count == 1);

  border = (struct border) { .focused = true, .shimmer_last_draw = 0.0 };
  bucket.value = &border;
  g_settings.shimmer_color_count = 2;
  g_settings.shimmer_fps = 1.0f;
  final_update_count = 0;
  animation_tick_callback(NULL, NULL);

  assert(final_update_count == 1);
  assert(border.needs_redraw);

  puts("animation completion and shimmer redraw: ok");
  return 0;
}
