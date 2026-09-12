#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool allocation_should_fail;

static void* test_malloc(size_t size) {
  return allocation_should_fail ? NULL : malloc(size);
}

#define malloc test_malloc
#include "../src/windows.c"
#undef malloc

pid_t g_pid;
struct settings g_settings;
CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);

static int ticker_start_count;
static int border_update_count;
static struct border* last_updated_border;
static int notification_cid;
static int notification_count;
static uint32_t notified_windows[2048];

struct settings* border_get_settings(struct border* border) {
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect* frame) {
  (void)cid;
  *frame = wid == 1
           ? (CGRect){{10.0, 20.0}, {100.0, 80.0}}
           : (CGRect){{300.0, 200.0}, {120.0, 90.0}};
  return kCGErrorSuccess;
}

int SLSMainConnectionID(void) {
  return 77;
}

CGError SLSRequestNotificationsForWindows(int cid,
                                          uint32_t* window_list,
                                          int window_count) {
  assert(window_count >= 0);
  assert(window_count <= (int)(sizeof(notified_windows)
                               / sizeof(notified_windows[0])));
  notification_cid = cid;
  notification_count = window_count;
  for (int i = 0; i < window_count; ++i) {
    notified_windows[i] = window_list[i];
  }
  return kCGErrorSuccess;
}

void animation_start_ticker(void) {
  ticker_start_count++;
}

void border_update(struct border* border, bool try_async) {
  (void)try_async;
  border_update_count++;
  last_updated_border = border;
}

void border_invalidate_props(struct border* border) {
  (void)border;
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

static void test_inactive_only_animation_redraws_new_focus(void) {
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

  g_settings = (struct settings) {
    .inactive_animation = ANIM_FADE,
    .animation_duration = 0.25f,
  };
  ticker_start_count = 0;
  border_update_count = 0;
  last_updated_border = NULL;

  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(!old_border.focused);
  assert(old_border.animating);
  assert(old_border.anim_mode == ANIM_FADE);
  assert(new_border.focused);
  assert(!new_border.animating);
  assert(new_border.anim_alpha == 1.0f);
  assert(border_update_count == 1);
  assert(last_updated_border == &new_border);
  assert(ticker_start_count == 1);
}

static void test_slide_uses_each_border_position(void) {
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

  g_settings = (struct settings) {
    .animation = ANIM_SLIDE,
    .animation_duration = 0.25f,
    .border_width = 5.0f,
    .inner_border_width = 5.0f,
    .border_position = BORDER_POSITION_OUTSIDE,
  };
  new_border.setting_override = g_settings;
  new_border.setting_override.enabled = true;
  new_border.setting_override.border_position = BORDER_POSITION_INSIDE;

  ticker_start_count = 0;
  border_update_count = 0;
  assert(windows_window_focus_with_mouse_state(&windows, new_key, false));
  assert(new_border.anim_origin_override);
  assert(new_border.anim_start_origin.x == -3.0f);
  assert(new_border.anim_start_origin.y == 7.0f);
  assert(new_border.anim_end_origin.x == 300.0f);
  assert(new_border.anim_end_origin.y == 200.0f);
}

static void test_large_window_notification_list(void) {
  enum { window_count = 1500 };
  struct bucket* buckets = calloc(window_count, sizeof(struct bucket));
  uint32_t* keys = calloc(window_count, sizeof(uint32_t));
  bool* seen = calloc(window_count, sizeof(bool));
  assert(buckets && keys && seen);

  struct bucket* table_buckets[1] = {};
  struct table windows = {
    .buckets = table_buckets,
    .capacity = 1,
    .count = window_count,
  };
  for (int i = 0; i < window_count; ++i) {
    keys[i] = (uint32_t)(i + 1);
    buckets[i].key = &keys[i];
    buckets[i].value = &buckets[i];
    buckets[i].next = table_buckets[0];
    table_buckets[0] = &buckets[i];
  }

  notification_count = -1;
  windows_update_notifications(&windows);
  assert(notification_cid == 77);
  assert(notification_count == window_count);
  for (int i = 0; i < notification_count; ++i) {
    assert(notified_windows[i] >= 1);
    assert(notified_windows[i] <= window_count);
    seen[notified_windows[i] - 1] = true;
  }
  for (int i = 0; i < window_count; ++i) assert(seen[i]);

  notification_count = -1;
  allocation_should_fail = true;
  windows_update_notifications(&windows);
  allocation_should_fail = false;
  assert(notification_count == -1);

  table_buckets[0] = NULL;
  windows.count = 0;
  windows_update_notifications(&windows);
  assert(notification_count == 0);

  free(seen);
  free(keys);
  free(buckets);
}

int main(void) {
  test_inactive_only_animation_redraws_new_focus();
  test_slide_uses_each_border_position();
  test_large_window_notification_list();
  puts("window focus and notification regressions: ok");
  return 0;
}
