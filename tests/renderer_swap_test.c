#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "../src/border.c"

struct settings g_settings;
struct table g_windows;
bool g_mission_control_active = false;

#define TEST_CID 1
#define TEST_TARGET_WID 42

// A live border window that was never built by this test run. Recreating it
// must produce a different wid, so these stay distinct from the stub output.
#define SEEDED_LAYER_WID 7001u
#define SEEDED_CG_WID 8001u
#define LAYER_WID_BASE 10000u
#define CG_WID_BASE 20000u

static int layer_create_count;
static int cg_create_count;
static int layer_destroy_count;
static int layer_update_count;
static int release_window_count;
static int layer_sentinel;

// --- window server stubs ----------------------------------------------------
// Everything SkyLight exposes is real and would be handed a connection id that
// belongs to nobody. Each stub keeps border_update_internal on its own path.

CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect* frame) {
  (void)cid;
  (void)wid;
  *frame = CGRectMake(100.0, 200.0, 300.0, 400.0);
  return kCGErrorSuccess;
}

CGError SLSWindowIsOrderedIn(int cid, uint32_t wid, bool* shown) {
  (void)cid;
  (void)wid;
  *shown = true;
  return kCGErrorSuccess;
}

CFArrayRef SLSCopySpacesForWindows(int cid, int selector, CFArrayRef window_list) {
  (void)cid;
  (void)selector;
  (void)window_list;
  return NULL;
}

CFStringRef SLSCopyManagedDisplayForWindow(int cid, uint32_t wid) {
  (void)cid;
  (void)wid;
  return NULL;
}

CFArrayRef SLSCopyManagedDisplays(int cid) {
  (void)cid;
  const void* displays[] = { CFSTR("test-display") };
  return CFArrayCreate(NULL, displays, 1, &kCFTypeArrayCallBacks);
}

uint64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef uuid) {
  (void)cid;
  (void)uuid;
  return 1;
}

CGError SLSMoveWindowsToManagedSpace(int cid, CFArrayRef window_list, uint64_t sid) {
  (void)cid;
  (void)window_list;
  (void)sid;
  return kCGErrorSuccess;
}

// No tags, so window_tags reports 0 and the border is never sticky.
CFTypeRef SLSWindowQueryWindows(int cid, CFArrayRef windows, uint32_t options) {
  (void)cid;
  (void)windows;
  (void)options;
  return NULL;
}

CGContextRef SLWindowContextCreate(int cid, uint32_t wid, CFDictionaryRef options) {
  (void)cid;
  (void)wid;
  (void)options;
  // A real bitmap so border_draw can rasterise into it after a rebuild.
  return CGBitmapContextCreate(NULL,
                               512,
                               512,
                               8,
                               0,
                               NULL,
                               kCGImageAlphaPremultipliedFirst);
}

CGError SLSSetWindowBackgroundBlurRadius(int cid, uint32_t wid, uint32_t radius) {
  (void)cid;
  (void)wid;
  (void)radius;
  return kCGErrorSuccess;
}

CGError SLSDisableUpdate(int cid) {
  (void)cid;
  return kCGErrorSuccess;
}

CGError SLSReenableUpdate(int cid) {
  (void)cid;
  return kCGErrorSuccess;
}

CGError SLSWindowFreezeWithOptions(int cid, uint32_t wid, CFTypeRef options) {
  (void)cid;
  (void)wid;
  (void)options;
  return kCGErrorSuccess;
}

CGError SLSWindowThaw(int cid, uint32_t wid) {
  (void)cid;
  (void)wid;
  return kCGErrorSuccess;
}

CGError SLSSetWindowShape(int cid, uint32_t wid, float x, float y, CFTypeRef shape) {
  (void)cid;
  (void)wid;
  (void)x;
  (void)y;
  (void)shape;
  return kCGErrorSuccess;
}

CGError SLSFlushWindowContentRegion(int cid, uint32_t wid, void* dirty) {
  (void)cid;
  (void)wid;
  (void)dirty;
  return kCGErrorSuccess;
}

CGError SLSReleaseWindow(int cid, uint32_t wid) {
  (void)cid;
  (void)wid;
  release_window_count++;
  return kCGErrorSuccess;
}

CFTypeRef SLSTransactionCreate(int cid) {
  (void)cid;
  return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
}

CGError SLSTransactionMoveWindowWithGroup(CFTypeRef transaction,
                                           uint32_t wid,
                                           CGPoint point) {
  (void)transaction;
  (void)wid;
  (void)point;
  return kCGErrorSuccess;
}

CGError SLSTransactionSetWindowTransform(CFTypeRef transaction,
                                         uint32_t wid,
                                         int not,
                                         int important,
                                         CGAffineTransform transform) {
  (void)transaction;
  (void)wid;
  (void)not;
  (void)important;
  (void)transform;
  return kCGErrorSuccess;
}

CGError SLSTransactionSetWindowLevel(CFTypeRef transaction, uint32_t wid, int level) {
  (void)transaction;
  (void)wid;
  (void)level;
  return kCGErrorSuccess;
}

CGError SLSTransactionSetWindowSubLevel(CFTypeRef transaction, uint32_t wid, int level) {
  (void)transaction;
  (void)wid;
  (void)level;
  return kCGErrorSuccess;
}

CGError SLSTransactionOrderWindow(CFTypeRef transaction,
                                  uint32_t wid,
                                  int order,
                                  uint32_t rel_wid) {
  (void)transaction;
  (void)wid;
  (void)order;
  (void)rel_wid;
  return kCGErrorSuccess;
}

CGError SLSTransactionCommit(CFTypeRef transaction, int synchronous) {
  (void)transaction;
  (void)synchronous;
  return kCGErrorSuccess;
}

CGError SLSSetWindowTags(int cid, uint32_t wid, uint64_t* tags, int tag_size) {
  (void)cid;
  (void)wid;
  (void)tags;
  (void)tag_size;
  return kCGErrorSuccess;
}

CGError SLSClearWindowTags(int cid, uint32_t wid, uint64_t* tags, int tag_size) {
  (void)cid;
  (void)wid;
  (void)tags;
  (void)tag_size;
  return kCGErrorSuccess;
}

// window_create is a header inline, so the CG path is faked at the SkyLight
// boundary it calls instead.
CGError SLSNewWindow(int cid,
                     int type,
                     float x,
                     float y,
                     CFTypeRef region,
                     uint32_t* wid) {
  (void)cid;
  (void)type;
  (void)x;
  (void)y;
  (void)region;
  cg_create_count++;
  *wid = CG_WID_BASE + (uint32_t)cg_create_count;
  return kCGErrorSuccess;
}

CGError SLSNewWindowWithOpaqueShapeAndContext(int cid,
                                             int type,
                                             CFTypeRef region,
                                             CFTypeRef opaque_shape,
                                             int options,
                                             uint64_t* tags,
                                             float x,
                                             float y,
                                             int tag_size,
                                             uint32_t* wid,
                                             void* context) {
  (void)cid;
  (void)type;
  (void)region;
  (void)opaque_shape;
  (void)options;
  (void)tags;
  (void)x;
  (void)y;
  (void)tag_size;
  (void)context;
  cg_create_count++;
  *wid = CG_WID_BASE + (uint32_t)cg_create_count;
  return kCGErrorSuccess;
}

CGError SLSSetWindowResolution(int cid, uint32_t wid, double res) {
  (void)cid;
  (void)wid;
  (void)res;
  return kCGErrorSuccess;
}

CGError SLSSetWindowOpacity(int cid, uint32_t wid, bool isOpaque) {
  (void)cid;
  (void)wid;
  (void)isOpaque;
  return kCGErrorSuccess;
}

CGError SLSWindowSetShadowProperties(uint32_t wid, CFDictionaryRef properties) {
  (void)wid;
  (void)properties;
  return kCGErrorSuccess;
}

int SLSMainConnectionID(void) {
  return TEST_CID;
}

// --- link-only stubs --------------------------------------------------------
// Nothing below is on the border_update_internal path; they exist so the whole
// of border.c resolves without dragging in animation.c and windows.c.

mach_port_t g_server_port = MACH_PORT_NULL;

void animation_init(struct animation* animation) {
  (void)animation;
}

void animation_stop(struct animation* animation) {
  (void)animation;
}

float animation_ease(enum animation_easing easing, float progress) {
  (void)easing;
  return progress;
}

// hashtable.c is not linked, and border.c never reaches the table on this path.
void* table_find(struct table* table, void* key) {
  (void)table;
  (void)key;
  return NULL;
}

void table_free(struct table* table) {
  (void)table;
}

// --- layer renderer stubs ---------------------------------------------------

struct layer_border* layer_border_create(int cid,
                                         CGRect frame,
                                         bool unmanaged,
                                         bool hidpi,
                                         uint32_t* wid) {
  (void)cid;
  (void)frame;
  (void)unmanaged;
  (void)hidpi;
  layer_create_count++;
  *wid = LAYER_WID_BASE + (uint32_t)layer_create_count;
  return (struct layer_border*)&layer_sentinel;
}

void layer_border_destroy(struct layer_border* layer) {
  (void)layer;
  layer_destroy_count++;
}

bool layer_border_update(struct layer_border* layer,
                         const struct layer_style* style,
                         CGSize size) {
  (void)layer;
  (void)style;
  (void)size;
  layer_update_count++;
  return true;
}

// --- harness ----------------------------------------------------------------

static void reset_counts(void) {
  layer_create_count = 0;
  cg_create_count = 0;
  layer_destroy_count = 0;
  layer_update_count = 0;
  release_window_count = 0;
}

// A single solid layer, no blur, no background, no animation: exactly the
// shape border_layer_style_expressible accepts.
static struct settings expressible_settings(bool force_cg) {
  struct color_style solid = { .stype = COLOR_STYLE_SOLID, .color = 0xff00ff00 };
  struct border_appearance appearance = {
    .layer_count = 1,
    .layers = { solid },
  };
  return (struct settings) {
    .enabled = true,
    .active_window = appearance,
    .inactive_window = appearance,
    .border_width = 4.0f,
    .inner_border_width = 2.0f,
    .double_border_gap = 1.0f,
    .border_style = BORDER_STYLE_ROUND,
    .border_position = BORDER_POSITION_OUTSIDE,
    .border_order = BORDER_ORDER_BELOW,
    .force_cg = force_cg,
  };
}

static struct border make_border(bool as_layer) {
  struct border border = { 0 };
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&border.mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);
  border.cid = TEST_CID;
  border.target_wid = TEST_TARGET_WID;
  border.sid = 1;
  border.inner_radius = 10.0f;
  border.needs_redraw = false;
  if (as_layer) {
    border.use_layer = true;
    border.layer = (struct layer_border*)&layer_sentinel;
    border.wid = SEEDED_LAYER_WID;
  } else {
    border.use_layer = false;
    border.layer = NULL;
    border.wid = SEEDED_CG_WID;
  }
  return border;
}

// renderer=cg on a border already built as a layer window.
static void test_layer_window_rebuilds_when_cg_is_requested(void) {
  reset_counts();
  struct border border = make_border(true);
  g_settings = expressible_settings(false);
  g_settings.force_cg = true;

  border_update_internal(&border, &g_settings);

  assert(border.use_layer == false);
  assert(border.wid == CG_WID_BASE + 1);
  assert(layer_create_count == 0);
  assert(cg_create_count == 1);
  assert(layer_destroy_count == 1);
  assert(release_window_count == 1);

  pthread_mutex_destroy(&border.mutex);
}

// renderer=layer on a border already built as a CG window.
static void test_cg_window_rebuilds_when_layer_is_requested(void) {
  reset_counts();
  struct border border = make_border(false);
  g_settings = expressible_settings(false);

  border_update_internal(&border, &g_settings);

  assert(border.use_layer == true);
  assert(border.wid == LAYER_WID_BASE + 1);
  assert(layer_create_count == 1);
  assert(cg_create_count == 0);
  assert(layer_destroy_count == 0);
  assert(release_window_count == 1);

  pthread_mutex_destroy(&border.mutex);
}

// Control: the renderer did not change, so the layer window must be reused.
static void test_unchanged_renderer_never_rebuilds(void) {
  reset_counts();
  struct border border = make_border(true);
  g_settings = expressible_settings(false);

  border_update_internal(&border, &g_settings);

  assert(border.use_layer == true);
  assert(border.wid == SEEDED_LAYER_WID);
  assert(layer_update_count == 1);
  assert(layer_create_count == 0);
  assert(cg_create_count == 0);
  assert(layer_destroy_count == 0);
  assert(release_window_count == 0);

  pthread_mutex_destroy(&border.mutex);
}

int main(void) {
  test_layer_window_rebuilds_when_cg_is_requested();
  test_cg_window_rebuilds_when_layer_is_requested();
  test_unchanged_renderer_never_rebuilds();
  puts("runtime renderer swap rebuilds the border window: ok");
  return 0;
}