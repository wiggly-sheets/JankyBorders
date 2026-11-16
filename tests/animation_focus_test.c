#include <assert.h>
#include <stdio.h>

#include "../src/windows.c"

pid_t g_pid;
struct settings g_settings;
CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);

static int ticker_start_count;
static struct border* last_updated_border;
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
  (void)try_async;
  last_updated_border = border;
}

void border_invalidate_props(struct border* border) {
  (void)border;
}

static void reset_globals(void) {
  ticker_start_count = 0;
  last_updated_border = NULL;
  fail_window_bounds = false;
  g_settings = (struct settings){0};
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

static void test_focus_transitions_animate_new_and_redraw_old(void) {
  reset_globals();
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
    .target_wid = new_key,
  };

  add_border(&windows, &old_bucket, &old_key, &old_border);
  add_border(&windows, &new_bucket, &new_key, &new_border);

  g_settings.animation = ANIM_FADE | ANIM_RAMP | ANIM_SLIDE | ANIM_PULSE;
  g_settings.animation_duration = 0.25f;
  g_settings.border_width = 5.0f;
  g_settings.inner_border_width = 5.0f;

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
  assert(last_updated_border == &old_border);
  assert(ticker_start_count == 1);

  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(ticker_start_count == 1);

  assert(windows_window_focus_with_mouse_state(&windows, old_key, true));
  assert(old_border.focused);
  assert(!new_border.focused);
  assert(!old_border.animating);
  assert(!new_border.animating);
  assert(!new_border.anim_origin_override);
  assert(ticker_start_count == 1);
}

static void test_slide_animation_uses_override_and_survives_bounds_failure(void) {
  reset_globals();
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
    .target_wid = new_key,
  };

  add_border(&windows, &old_bucket, &old_key, &old_border);
  add_border(&windows, &new_bucket, &new_key, &new_border);

  g_settings.animation = ANIM_SLIDE | ANIM_PULSE;
  g_settings.animation_duration = 0.25f;
  g_settings.border_width = 5.0f;
  g_settings.inner_border_width = 5.0f;
  g_settings.border_position = BORDER_POSITION_OUTSIDE;

  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(!old_border.animating);
  assert(new_border.animating);
  assert(new_border.anim_mode == (ANIM_SLIDE | ANIM_PULSE));
  assert(last_updated_border == &old_border);

  assert(windows_window_focus_with_mouse_state(&windows, old_key, true));
  new_border.setting_override = g_settings;
  new_border.setting_override.enabled = true;
  new_border.setting_override.animation = ANIM_SLIDE;
  new_border.setting_override.animation_duration = 0.75f;
  new_border.setting_override.border_width = 2.0f;
  new_border.setting_override.inner_border_width = 2.0f;
  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(new_border.anim_mode == ANIM_SLIDE);
  assert(new_border.anim_duration == 0.75f);
  assert(new_border.anim_end_origin.x == 290.0f);
  assert(new_border.anim_end_origin.y == 190.0f);

  assert(windows_window_focus_with_mouse_state(&windows, old_key, true));
  fail_window_bounds = true;
  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(new_border.animating);
  assert(!new_border.anim_origin_override);
}

static void test_focus_clears_every_other_focused_border(void) {
  reset_globals();
  struct bucket* buckets[1] = {};
  struct table windows = {
    .buckets = buckets,
    .capacity = 1,
  };
  uint32_t old_key = 1;
  uint32_t new_key = 2;
  uint32_t other_key = 3;
  struct bucket old_bucket = {};
  struct bucket new_bucket = {};
  struct bucket other_bucket = {};
  struct border old_border = {
    .cid = 1,
    .focused = true,
    .target_wid = old_key,
  };
  struct border new_border = {
    .cid = 1,
    .target_wid = new_key,
  };
  struct border other_border = {
    .cid = 1,
    .focused = true,
    .target_wid = other_key,
  };

  add_border(&windows, &old_bucket, &old_key, &old_border);
  add_border(&windows, &other_bucket, &other_key, &other_border);
  add_border(&windows, &new_bucket, &new_key, &new_border);

  g_settings.animation = ANIM_SLIDE | ANIM_PULSE;
  g_settings.animation_duration = 0.25f;

  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(!old_border.focused);
  assert(!other_border.focused);
  assert(new_border.focused);
}

int main(void) {
  test_focus_transitions_animate_new_and_redraw_old();
  test_slide_animation_uses_override_and_survives_bounds_failure();
  test_focus_clears_every_other_focused_border();
  puts("animation focus transition: ok");
  return 0;
}
