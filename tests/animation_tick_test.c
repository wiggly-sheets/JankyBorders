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
static float last_animating_progress;

void border_update_animating(struct border* border, float progress) {
  (void)border;
  last_animating_progress = progress;
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

  assert(animation_pulse_scale(0.0f) == 1.0f);
  assert(animation_pulse_scale(0.5f) == 2.0f);
  assert(animation_pulse_scale(1.0f) == 1.0f);
  assert(animation_pulse_width(1.0f, 0.0f) == 1.0f);
  assert(animation_pulse_width(1.0f, 0.5f) == 7.0f);
  assert(animation_pulse_width(1.0f, 1.0f) == 1.0f);
  assert(animation_pulse_width(4.0f, 0.5f) == 7.0f);
  assert(animation_pulse_width(8.0f, 0.5f) - 8.0f
         < animation_pulse_width(4.0f, 0.5f) - 4.0f);
  assert(animation_lerp(-3.0f, 287.0f, 0.5f) == 142.0f);

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

  // In-progress: one update_animating call, and alpha follows focus direction.
  struct border mid = {
    .focused = true,
    .animating = true,
    .anim_mode = ANIM_FADE,
    .anim_start = CACurrentMediaTime() - 2.5,
    .anim_duration = 10.0f,
    .anim_alpha = 0.0f,
  };
  bucket.value = &mid;
  animating_update_count = 0;
  final_update_count = 0;
  animation_tick_callback(NULL, NULL);

  assert(mid.animating);
  assert(mid.anim_mode == ANIM_FADE);
  assert(animating_update_count == 1);
  assert(last_animating_progress > 0.2f && last_animating_progress < 0.3f);
  assert(mid.anim_alpha > 0.2f && mid.anim_alpha < 0.3f);
  assert(fabsf(mid.anim_alpha - last_animating_progress) < 0.0001f);
  assert(final_update_count == 0);

  mid.focused = false;
  mid.anim_start = CACurrentMediaTime() - 2.5;
  mid.anim_alpha = 0.75f;
  animating_update_count = 0;
  final_update_count = 0;
  animation_tick_callback(NULL, NULL);

  assert(mid.animating);
  assert(animating_update_count == 1);
  assert(last_animating_progress > 0.2f && last_animating_progress < 0.3f);
  assert(mid.anim_alpha > 0.7f && mid.anim_alpha < 0.8f);
  assert(fabsf(mid.anim_alpha - (1.0f - last_animating_progress)) < 0.0001f);
  assert(final_update_count == 0);

  puts("animation easing, pulse math, and tick updates: ok");
  return 0;
}
