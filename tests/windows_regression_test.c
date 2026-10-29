#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool allocation_should_fail;

static void* test_malloc(size_t size) {
  return allocation_should_fail ? NULL : malloc(size);
}

#define malloc test_malloc
#define proc_name test_proc_name
#define CGGetActiveDisplayList test_cg_get_active_display_list
#define CGDisplayBounds test_cg_display_bounds
#include "../src/windows.c"
#undef CGDisplayBounds
#undef CGGetActiveDisplayList
#undef proc_name
#undef malloc

pid_t g_pid;
struct settings g_settings;
struct table g_windows;
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

#define TEST_MC_WID            42u
#define TEST_MC_PARTIAL_WID    43u
// A second WindowManager window that also covers a display exactly, so a level
// the detector never learned can be tested without reusing the tracked backdrop.
#define TEST_MC_OTHER_WID      44u
#define TEST_MC_DISPLAY_BOUNDS (CGRect){{0.0, 0.0}, {1920.0, 1080.0}}
#define TEST_MC_NO_WINDOW      ((CGError)1001)

CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect* frame) {
  (void)cid;
  if (wid == TEST_MC_WID || wid == TEST_MC_OTHER_WID) {
    *frame = TEST_MC_DISPLAY_BOUNDS;
    return kCGErrorSuccess;
  }
  if (wid == TEST_MC_PARTIAL_WID) {
    // Owned by the right process at the right level, but it covers only part
    // of the display, so it is a normal overlay and not a Mission Control
    // backdrop.
    *frame = (CGRect){{0.0, 0.0}, {960.0, 1080.0}};
    return kCGErrorSuccess;
  }
  *frame = wid == 1
           ? (CGRect){{10.0, 20.0}, {100.0, 80.0}}
           : (CGRect){{300.0, 200.0}, {120.0, 90.0}};
  return kCGErrorSuccess;
}

// --- Mission Control backdrop harness -------------------------------------
// The backdrop predicate needs level, owner pid -> process name, and bounds
// that equal an active display. Each leg is stubbed so a mismatch on any one
// of the three can be isolated.

static int stub_level;
static int stub_owner_pid;
static const char* stub_owner_name;
static bool stub_window_alive;

int test_proc_name(int pid, void* buffer, uint32_t buffersize) {
  (void)pid;
  if (!stub_owner_name) return -1;
  snprintf(buffer, buffersize, "%s", stub_owner_name);
  return (int)strlen(stub_owner_name);
}

CGError test_cg_get_active_display_list(uint32_t max_displays,
                                       uint32_t* active_displays,
                                       uint32_t* display_count) {
  if (display_count) *display_count = 1;
  if (max_displays > 0 && active_displays) active_displays[0] = 1;
  return kCGErrorSuccess;
}

CGRect test_cg_display_bounds(uint32_t display) {
  (void)display;
  return TEST_MC_DISPLAY_BOUNDS;
}

CGError SLSGetWindowOwner(int cid, uint32_t wid, int* out_cid) {
  (void)cid;
  if (wid != TEST_MC_WID && wid != TEST_MC_PARTIAL_WID
      && wid != TEST_MC_OTHER_WID) {
    return TEST_MC_NO_WINDOW;
  }
  if (!stub_window_alive) return TEST_MC_NO_WINDOW;
  *out_cid = 99;
  return kCGErrorSuccess;
}

CGError SLSConnectionGetPID(int cid, pid_t* pid) {
  (void)cid;
  *pid = (pid_t)stub_owner_pid;
  return kCGErrorSuccess;
}

// window_level() builds a real CFArray, so the query stubs hand back throwaway
// CoreFoundation objects of the right shape instead of faking pointers.
static CFTypeRef mc_test_query(CFArrayRef windows) {
  (void)windows;
  return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
}

static CFTypeRef mc_test_iterator(CFTypeRef query) {
  (void)query;
  return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
}

bool SLSWindowIteratorAdvance(CFTypeRef iterator) {
  (void)iterator;
  return true;
}

int SLSWindowIteratorGetLevel(CFTypeRef iterator) {
  (void)iterator;
  return stub_level;
}

CFTypeRef SLSWindowQueryWindows(int cid,
                                CFArrayRef windows,
                                uint32_t options) {
  (void)cid; (void)options;
  return mc_test_query(windows);
}

CFTypeRef SLSWindowQueryResultCopyWindows(CFTypeRef window_query) {
  return mc_test_iterator(window_query);
}

void border_hide(struct border* border) {
  (void)border;
}

// Prune's exit redraws through windows_draw_borders_on_current_spaces. One
// managed display and a NULL window list walk that path without a real
// WindowServer. The display must be non-empty: a zero-length VLA down there
// trips UBSan, which is a pre-existing issue not worth pulling into this port.
CFArrayRef SLSCopyManagedDisplays(int cid) {
  (void)cid;
  const void* displays[] = { CFSTR("test-display") };
  return CFArrayCreate(NULL, displays, 1, &kCFTypeArrayCallBacks);
}

uint64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef uuid) {
  (void)cid; (void)uuid;
  return 1;
}

CFArrayRef SLSCopyWindowsWithOptionsAndTags(int cid,
                                            uint32_t owner,
                                            CFArrayRef spaces,
                                            uint32_t options,
                                            uint64_t* set_tags,
                                            uint64_t* clear_tags) {
  (void)cid; (void)owner; (void)spaces; (void)options;
  (void)set_tags; (void)clear_tags;
  return NULL;
}

struct border* border_create() {
  return NULL;
}

void border_destroy(struct border* border) {
  (void)border;
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

// Every Mission Control test starts from the seeded level with nothing learned
// and nothing tracked, so none of them inherit state from the others.
static void reset_mission_control_state(void) {
  g_mission_control_active = false;
  g_mc_backdrop_count = 0;
  g_mc_backdrop_level = 19;
  g_mc_backdrop_level_learned = false;
  g_settings = (struct settings){0};
}

static void test_mission_control_backdrop_predicate(void) {
  reset_mission_control_state();
  stub_window_alive = true;
  stub_owner_pid = 4242;
  stub_owner_name = "WindowManager";

  stub_level = 19;
  assert(mission_control_is_backdrop(77, TEST_MC_WID));
  assert(g_mc_backdrop_level == 19);

  // Wrong level: some other app's overlay is not a backdrop.
  stub_level = 20;
  assert(!mission_control_is_backdrop(77, TEST_MC_WID));
  stub_level = 18;
  assert(!mission_control_is_backdrop(77, TEST_MC_WID));
  stub_level = 19;

  // Right level, wrong owner process.
  stub_owner_name = "Dock";
  assert(!mission_control_is_backdrop(77, TEST_MC_WID));
  stub_owner_name = NULL;
  assert(!mission_control_is_backdrop(77, TEST_MC_WID));
  stub_owner_name = "WindowManager";

  // Right level and owner, but bounds covering only part of a display.
  assert(!mission_control_is_backdrop(77, TEST_MC_PARTIAL_WID));

  // Bounds matching no active display at all.
  assert(!mission_control_is_backdrop(77, 1));

  // Dead window is never a backdrop.
  stub_window_alive = false;
  assert(!mission_control_is_backdrop(77, TEST_MC_WID));
  stub_window_alive = true;
}

// Regression: the old detector hardcoded level 19 and rejected everything else,
// so on macOS 27 no backdrop ever matched and borders never hid. The level has
// to be learned from the first credible window instead.
static void test_mission_control_learns_backdrop_level(void) {
  reset_mission_control_state();
  stub_window_alive = true;
  stub_owner_pid = 4242;
  stub_owner_name = "WindowManager";

  stub_level = 27;
  assert(mission_control_is_backdrop(77, TEST_MC_WID));
  assert(g_mc_backdrop_level_learned);
  assert(g_mc_backdrop_level == 27);

  // The learned level now gates: the same window at any other level is not a
  // backdrop any more.
  stub_level = 19;
  assert(!mission_control_is_backdrop(77, TEST_MC_WID));
  stub_level = 27;
  assert(mission_control_is_backdrop(77, TEST_MC_WID));
}

static void test_mission_control_relearns_level_for_tracked_backdrop(void) {
  reset_mission_control_state();
  stub_window_alive = true;
  stub_owner_pid = 4242;
  stub_owner_name = "WindowManager";

  stub_level = 7;
  windows_mission_control_window_created(77, TEST_MC_WID);
  assert(g_mission_control_active);
  assert(g_mc_backdrop_count == 1);
  assert(g_mc_backdrop_level == 7);

  // A backdrop we already track reporting a new level re-learns it, rather than
  // leaving a stale level to reject every later backdrop.
  stub_level = 31;
  assert(mission_control_is_backdrop(77, TEST_MC_WID));
  assert(g_mc_backdrop_level == 31);

  // But an untracked WindowManager window at some unrelated level must not be
  // able to move the goalposts.
  stub_level = 3;
  assert(!mission_control_is_backdrop(77, TEST_MC_OTHER_WID));
  assert(g_mc_backdrop_level == 31);
}

static void test_mission_control_state_machine(void) {
  reset_mission_control_state();

  stub_window_alive = true;
  stub_owner_pid = 4242;
  stub_owner_name = "WindowManager";
  stub_level = 19;

  // A non-backdrop window must not flip the state.
  stub_owner_name = "Dock";
  windows_mission_control_window_created(77, TEST_MC_WID);
  assert(!g_mission_control_active);
  assert(g_mc_backdrop_count == 0);
  stub_owner_name = "WindowManager";

  // Backdrop created -> enter, tracked exactly once.
  windows_mission_control_window_created(77, TEST_MC_WID);
  assert(g_mission_control_active);
  assert(g_mc_backdrop_count == 1);

  windows_mission_control_window_created(77, TEST_MC_WID);
  assert(g_mc_backdrop_count == 1);

  // Prune with the backdrop still alive must stay in Mission Control.
  windows_mission_control_prune(77);
  assert(g_mission_control_active);
  assert(g_mc_backdrop_count == 1);

  // Destroy event for a window we never tracked is ignored.
  windows_mission_control_window_destroyed(77, 999);
  assert(g_mission_control_active);
  assert(g_mc_backdrop_count == 1);

  // SkyLight emits destroy when a backdrop moves between spaces. The window
  // still exists at that point, so prune must NOT treat it as MC exiting.
  windows_mission_control_window_destroyed(77, TEST_MC_WID);
  assert(g_mission_control_active);
  assert(g_mc_backdrop_count == 1);

  // Only once the window is really gone does prune exit and redraw borders.
  stub_window_alive = false;
  windows_mission_control_prune(77);
  assert(!g_mission_control_active);
  assert(g_mc_backdrop_count == 0);

  // Prune is a no-op while idle.
  windows_mission_control_prune(77);
  assert(!g_mission_control_active);
  assert(g_mc_backdrop_count == 0);
}

// --- window_suitable predicate tests ----------------------------------------

static uint64_t test_tags;
static uint64_t test_attrs;
static uint32_t test_parent;

uint64_t SLSWindowIteratorGetTags(CFTypeRef iterator) {
  (void)iterator;
  return test_tags;
}

uint64_t SLSWindowIteratorGetAttributes(CFTypeRef iterator) {
  (void)iterator;
  return test_attrs;
}

uint32_t SLSWindowIteratorGetParentID(CFTypeRef iterator) {
  (void)iterator;
  return test_parent;
}

static void test_window_suitable_predicate(void) {
  uint64_t visible_attr = 0x2;
  uint64_t visible_tagbit = 0x400000000000000ULL;

  // DOCUMENT + IGNORES_CYCLE -> suitable (regression)
  test_tags = WINDOW_TAG_DOCUMENT | WINDOW_TAG_IGNORES_CYCLE | visible_tagbit;
  test_attrs = 0;
  test_parent = 0;
  assert(window_suitable(NULL));

  // DOCUMENT, no IGNORES_CYCLE -> suitable
  test_tags = WINDOW_TAG_DOCUMENT | visible_tagbit;
  test_attrs = 0;
  test_parent = 0;
  assert(window_suitable(NULL));

  // FLOATING|MODAL + IGNORES_CYCLE -> not suitable (transient overlay filtered)
  test_tags = WINDOW_TAG_FLOATING | WINDOW_TAG_MODAL | WINDOW_TAG_IGNORES_CYCLE | visible_tagbit;
  test_attrs = 0;
  test_parent = 0;
  assert(!window_suitable(NULL));

  // FLOATING|MODAL, no IGNORES_CYCLE -> suitable
  test_tags = WINDOW_TAG_FLOATING | WINDOW_TAG_MODAL | visible_tagbit;
  test_attrs = 0;
  test_parent = 0;
  assert(window_suitable(NULL));

  // ATTACHED -> not suitable
  test_tags = WINDOW_TAG_DOCUMENT | visible_tagbit | WINDOW_TAG_ATTACHED;
  test_attrs = 0;
  test_parent = 0;
  assert(!window_suitable(NULL));

  // parent_wid != 0 -> not suitable
  test_tags = WINDOW_TAG_DOCUMENT | visible_tagbit;
  test_attrs = 0;
  test_parent = 42;
  assert(!window_suitable(NULL));

  // Also verify visible signal via attributes bit
  test_tags = WINDOW_TAG_DOCUMENT;
  test_attrs = visible_attr;
  test_parent = 0;
  assert(window_suitable(NULL));
}

int main(void) {
  test_inactive_only_animation_redraws_new_focus();
  test_slide_uses_each_border_position();
  test_large_window_notification_list();
  test_mission_control_backdrop_predicate();
  test_mission_control_learns_backdrop_level();
  test_mission_control_relearns_level_for_tracked_backdrop();
  test_mission_control_state_machine();
  test_window_suitable_predicate();
  puts("window focus and notification regressions: ok");
  return 0;
}
