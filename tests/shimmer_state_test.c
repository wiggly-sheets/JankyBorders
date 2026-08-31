#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/animation.c"

struct settings g_settings;
struct table g_windows;

static int border_update_count;

struct settings* border_get_settings(struct border* border) {
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

void border_update_animating(struct border* border, float progress) {
  (void)border;
  (void)progress;
}

void border_update(struct border* border, bool try_async) {
  (void)border;
  (void)try_async;
  border_update_count++;
}

static void assert_interval(double expected) {
  assert(g_anim_timer);
  assert(fabs(CFRunLoopTimerGetInterval(g_anim_timer) - expected) < 1e-6);
}

int main(void) {
  struct border border = {
    .focused = true,
  };
  struct bucket bucket = {
    .value = &border,
  };
  struct bucket* buckets[] = { &bucket };
  g_windows = (struct table) {
    .buckets = buckets,
    .capacity = 1,
  };

  g_settings.inactive_shimmer_color_count = 2;
  g_settings.shimmer_fps = 120.0f;
  animation_tick_callback(NULL, NULL);
  assert(border_update_count == 0);
  assert(!border.needs_redraw);

  border.focused = false;
  animation_tick_callback(NULL, NULL);
  assert(border_update_count == 1);
  assert(border.needs_redraw);

  border.focused = true;
  border.needs_redraw = false;
  border.shimmer_last_draw = 0.0;
  g_settings.inactive_shimmer_color_count = 0;
  g_settings.shimmer_color_count = 2;
  animation_tick_callback(NULL, NULL);
  assert(border_update_count == 2);
  assert(border.needs_redraw);

  animation_start_ticker();
  assert_interval(1.0 / 120.0);
  animation_stop_ticker();

  g_settings.shimmer_fps = 30.0f;
  animation_start_ticker();
  assert_interval(1.0 / 30.0);

  border.animating = true;
  animation_start_ticker();
  assert_interval(1.0 / 60.0);
  border.animating = false;
  animation_tick_callback(NULL, NULL);
  assert_interval(1.0 / 30.0);
  animation_stop_ticker();

  g_settings.shimmer_color_count = 0;
  border.animating = true;
  animation_start_ticker();
  assert_interval(1.0 / 60.0);
  animation_stop_ticker();

  puts("state-specific shimmer scheduling: ok");
  return 0;
}
