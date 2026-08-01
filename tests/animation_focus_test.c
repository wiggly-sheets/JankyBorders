#include <assert.h>
#include <stdio.h>

#include "../src/windows.c"

pid_t g_pid;
struct settings g_settings;
CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);

static int ticker_start_count;
static int border_update_count;
static bool fail_window_bounds;

struct settings* border_get_settings(struct border* border) {
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect* frame) {
  (void)cid;
  if (fail_window_bounds) return kCGErrorFailure;
  *frame = wid == 1
           ? (CGRect){{10.0, 20.0}, {100.0, 80.0}}
           : (CGRect){{300.0, 200.0}, {120.0, 90.0}};
  return kCGErrorSuccess;
}

void animation_start_ticker(void) {
  ticker_start_count++;
}

void border_update(struct border* border, bool try_async) {
  (void)border;
  (void)try_async;
  border_update_count++;
}

static void add_border(struct table* windows,
                       struct bucket* bucket,
                       uint32_t* key,
                       struct border* border) {
  bucket->key = key;
  bucket->value = border;
  bucket->next = windows->buckets[0];
  windows->buckets[0] = bucket;
}

int main(void) {
  struct bucket* buckets[1] = {};
  struct table windows = {
    .buckets = buckets,
    .capacity = 1,
  };
  uint32_t old_key = 1;
  uint32_t new_key = 2;
  struct bucket old_bucket = {};
  struct bucket new_bucket = {};
  struct border old_border = {
    .cid = 1,
    .focused = true,
    .target_wid = old_key,
  };
  struct border new_border = {
    .cid = 1,
    .focused = false,
    .target_wid = new_key,
  };

  add_border(&windows, &old_bucket, &old_key, &old_border);
  add_border(&windows, &new_bucket, &new_key, &new_border);

  g_settings.animation = ANIM_FADE | ANIM_RAMP | ANIM_SLIDE | ANIM_PULSE;
  g_settings.animation_duration = 0.25f;
  g_settings.border_width = 5.0f;

  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(!old_border.focused);
  assert(new_border.focused);
  assert(!old_border.animating);
  assert(new_border.animating);
  assert(old_border.anim_mode == 0);
  assert(new_border.anim_mode
         == (ANIM_FADE | ANIM_RAMP | ANIM_SLIDE | ANIM_PULSE));
  assert(new_border.anim_origin_override);
  assert(new_border.anim_current_origin.x
         == new_border.anim_start_origin.x);
  assert(new_border.anim_current_origin.y
         == new_border.anim_start_origin.y);
  assert(new_border.anim_start_origin.x == -3.0);
  assert(new_border.anim_start_origin.y == 7.0);
  assert(new_border.anim_end_origin.x == 287.0);
  assert(new_border.anim_end_origin.y == 187.0);
  assert(ticker_start_count == 1);
  assert(border_update_count == 1);

  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(ticker_start_count == 1);

  assert(windows_window_focus_with_mouse_state(&windows, old_key, true));
  assert(old_border.focused);
  assert(!new_border.focused);
  assert(!old_border.animating);
  assert(!new_border.animating);
  assert(!new_border.anim_origin_override);
  assert(ticker_start_count == 1);

  g_settings.animation = ANIM_SLIDE | ANIM_PULSE;
  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(!old_border.animating);
  assert(new_border.animating);
  assert(new_border.anim_mode == (ANIM_SLIDE | ANIM_PULSE));
  assert(border_update_count == 4);

  assert(animation_pulse_scale(0.0f) == 1.0f);
  assert(animation_pulse_scale(0.5f) == 2.0f);
  assert(animation_pulse_scale(1.0f) == 1.0f);
  assert(animation_pulse_width(1.0f, 0.0f) == 1.0f);
  assert(animation_pulse_width(1.0f, 0.5f) == 7.0f);
  assert(animation_pulse_width(1.0f, 1.0f) == 1.0f);
  assert(animation_pulse_width(4.0f, 0.5f) == 7.0f);
  assert(animation_pulse_width(8.0f, 0.5f) - 8.0f
         < animation_pulse_width(4.0f, 0.5f) - 4.0f);
  assert(animation_lerp(new_border.anim_start_origin.x,
                        new_border.anim_end_origin.x,
                        0.5f) == 142.0f);

  struct bucket* stale_buckets[1] = {};
  struct table stale_windows = {
    .buckets = stale_buckets,
    .capacity = 1,
  };
  uint32_t stale_key = 3;
  struct bucket stale_bucket = {};
  struct border stale_border = {
    .cid = 1,
    .focused = true,
    .target_wid = stale_key,
  };
  old_border.focused = true;
  new_border.focused = false;
  add_border(&stale_windows, &old_bucket, &old_key, &old_border);
  add_border(&stale_windows, &stale_bucket, &stale_key, &stale_border);
  add_border(&stale_windows, &new_bucket, &new_key, &new_border);

  assert(windows_window_focus_with_mouse_state(&stale_windows, new_key, false));
  assert(!old_border.focused);
  assert(!stale_border.focused);
  assert(new_border.focused);

  assert(windows_window_focus_with_mouse_state(&stale_windows, old_key, true));
  new_border.setting_override = g_settings;
  new_border.setting_override.enabled = true;
  new_border.setting_override.animation = ANIM_SLIDE;
  new_border.setting_override.animation_duration = 0.75f;
  new_border.setting_override.border_width = 2.0f;
  assert(windows_window_focus_with_mouse_state(&stale_windows, new_key, false));
  assert(new_border.anim_mode == ANIM_SLIDE);
  assert(new_border.anim_duration == 0.75f);
  assert(new_border.anim_end_origin.x == 290.0f);
  assert(new_border.anim_end_origin.y == 190.0f);

  assert(windows_window_focus_with_mouse_state(&stale_windows, old_key, true));
  fail_window_bounds = true;
  assert(windows_window_focus_with_mouse_state(&stale_windows, new_key, false));
  assert(new_border.animating);
  assert(!new_border.anim_origin_override);

  puts("animation focus transition: ok");
  return 0;
}
