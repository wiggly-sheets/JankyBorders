#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "border.h"
#include "misc/window.h"
#include <string.h>

#include <pthread.h>
#include <time.h>

extern struct table g_windows;
extern pid_t g_pid;

// Requests are coalesced into one pending run at the earliest deadline asked
// for, plus a trailing run at the latest one, so that a storm of events can
// neither push the run out nor drop the longer delay it asked for.
struct coalesced_job {
  bool pending;
  uint32_t generation;
  uint64_t deadline;
  uint64_t latest;
};

#define COALESCE_SLACK_NS (1 * NSEC_PER_MSEC)

static void schedule_coalesced(struct coalesced_job* job, uint64_t delay_us, dispatch_block_t action) {
  if (!pthread_main_np()) {
    dispatch_async(dispatch_get_main_queue(), ^{
      schedule_coalesced(job, delay_us, action);
    });
    return;
  }

  uint64_t delay_ns = delay_us * NSEC_PER_USEC;
  uint64_t deadline = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + delay_ns;

  if (job->latest < deadline) job->latest = deadline;

  if (job->pending && job->deadline <= deadline) return;

  job->pending = true;
  job->deadline = deadline;
  uint32_t generation = ++job->generation;

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)delay_ns),
                 dispatch_get_main_queue(), ^{
    if (job->generation != generation) return;
    job->pending = false;
    action();

    uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    uint64_t latest = job->latest;
    job->latest = 0;
    if (latest > now + COALESCE_SLACK_NS) {
      schedule_coalesced(job, (latest - now) / NSEC_PER_USEC, action);
    }
  });
}

static struct coalesced_job g_focus_job;
static struct coalesced_job g_space_job;

static inline void schedule_focus_update(uint64_t delay_us) {
  schedule_coalesced(&g_focus_job, delay_us, ^{
    windows_determine_and_focus_active_window(&g_windows);
  });
}

static inline void schedule_space_update(uint64_t delay_us) {
  schedule_coalesced(&g_space_job, delay_us, ^{
    windows_draw_borders_on_current_spaces(&g_windows);
  });
}

// yabai swap/warp/move emits non-atomic MOVE/RESIZE pairs: MOVE syncs
// origin-only while RESIZE lags 5ms behind, so pos jumps now and size follows
// = 2-step tear; A also lands on B's rect before B leaves = overlap.
// Storm = >=2 distinct MOVE wids in 30ms (swap) or MOVE+RESIZE same wid in
// ~10ms (pos/size split). Swap storms hide + single sync after 50ms quiet
// (yabai anim 0.05); a lone split just syncs now to skip the 5ms lag.
// Pure single-window drags (MOVE only, same wid) stay on the fast path.
// ponytail: fixed 50ms quiet, not yabai's live anim duration; read it live if this misfires.
#define SWAP_STORM_NS (30 * NSEC_PER_MSEC)
#define MOVE_RESIZE_JOIN_NS (10 * NSEC_PER_MSEC)
#define SWAP_QUIET_US 50000
#define SWAP_MAX_WIDS 8

static struct coalesced_job g_swap_job;
static uint32_t g_swap_wids[SWAP_MAX_WIDS];
static int g_swap_count;
static uint32_t g_last_move_wid;
static uint64_t g_last_move_time;

static bool swap_pending_add(uint32_t wid) {
  for (int i = 0; i < g_swap_count; ++i) {
    if (g_swap_wids[i] == wid) return true;
  }
  if (g_swap_count >= SWAP_MAX_WIDS) return false;
  g_swap_wids[g_swap_count++] = wid;
  return true;
}

static void swap_flush(void) {
  uint32_t wids[SWAP_MAX_WIDS];
  int count = g_swap_count > SWAP_MAX_WIDS ? SWAP_MAX_WIDS : g_swap_count;
  memcpy(wids, g_swap_wids, (size_t)count * sizeof(uint32_t));
  g_swap_count = 0;
  for (int i = 0; i < count; ++i) {
    struct border* border = table_find(&g_windows, &wids[i]);
    if (border) border_update(border, false);
  }
}

static void swap_defer(struct table* windows, uint32_t wid) {
  if (!swap_pending_add(wid)) {
    struct border* border = table_find(windows, &wid);
    if (border) border_update(border, false);
    return;
  }
  windows_window_hide(windows, wid);
  schedule_coalesced(&g_swap_job, SWAP_QUIET_US, ^{
    swap_flush();
  });
}

#ifdef DEBUG
static void dump_event(void* data, size_t data_length) {
  for (int i = 0; i < data_length; i++) {
    printf("%02x ", *((unsigned char*)data + i));
  }
  printf("\n");
}

static void event_watcher(uint32_t event, void* data, size_t data_length, void* context) {
  static int count = 0;
  printf("(%d) Event: %d; Payload:\n", ++count, event);
  dump_event(data, data_length);
}
#endif

struct window_spawn_data {
  uint64_t sid;
  uint32_t wid;
};

static bool is_own_window(int cid, uint32_t wid) {
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);
  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  return pid == g_pid;
}

static void window_spawn_handler(uint32_t event,
                                 void* data,
                                 size_t data_length,
                                 int cid) {
  if (data_length < sizeof(struct window_spawn_data)) return;

  struct window_spawn_data spawn_data;
  memcpy(&spawn_data, data, sizeof(spawn_data));
  struct table* windows = &g_windows;
  uint32_t wid = spawn_data.wid;
  uint64_t sid = spawn_data.sid;

  if (!wid || !sid || is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_CREATE) {
    if (windows_window_create(windows, wid, sid)) {
      debug("Window Created: %d %d\n", wid, sid);
      windows_determine_and_focus_active_window(windows);
    }
  } else if (event == EVENT_WINDOW_DESTROY) {
    if (windows_window_destroy(windows, wid, sid)) {
      debug("Window Destroyed: %d %d\n", wid, sid);
    }
    windows_determine_and_focus_active_window(windows);
  }
}

static void window_modify_handler(uint32_t event, uint32_t* window_id, size_t _, int cid) {
  uint32_t wid = *window_id;
  struct table* windows = &g_windows;

  if (is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_MOVE) {
    debug("Window Move: %d\n", wid);
    uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    uint32_t prev_wid = g_last_move_wid;
    uint64_t prev_time = g_last_move_time;
    g_last_move_wid = wid;
    g_last_move_time = now;
    bool storm = g_swap_job.pending
                 || (prev_wid && prev_wid != wid
                     && now - prev_time < SWAP_STORM_NS);
    if (!storm) {
      windows_window_move(windows, wid);
      return;
    }
    if (prev_wid && prev_wid != wid && now - prev_time < SWAP_STORM_NS) {
      swap_defer(windows, prev_wid);
    }
    swap_defer(windows, wid);
  } else if (event == EVENT_WINDOW_RESIZE) {
    debug("Window Resize: %d\n", wid);
    if (g_swap_job.pending) {
      swap_defer(windows, wid);
    } else if (g_last_move_wid && wid == g_last_move_wid
               && clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - g_last_move_time
                  < MOVE_RESIZE_JOIN_NS) {
      struct border* border = table_find(windows, &wid);
      if (border) border_update(border, false);
    } else {
      windows_window_update(windows, wid);
    }
  } else if (event == EVENT_WINDOW_REORDER) {
    debug("Window Reorder (and focus): %d\n", wid);
    // yabai autoraise emits reorder before focus state settles. Updating here
    // orders a border/blur companion with stale focus state and can expose a
    // one-sided blur seam. Let the subsequent focus pass order both surfaces.
    windows_window_refresh(windows, wid);
    schedule_focus_update(10000);
  } else if (event == EVENT_WINDOW_LEVEL) {
    debug("Window Level: %d\n", wid);
    windows_window_refresh(windows, wid);
  } else if (event == EVENT_WINDOW_TITLE || event == EVENT_WINDOW_UPDATE) {
    debug("Window Focus\n");
    schedule_focus_update(50000);
  } else if (event == EVENT_WINDOW_UNHIDE) {
    debug("Window Unhide: %d\n", wid);
    windows_window_unhide(windows, wid);
  } else if (event == EVENT_WINDOW_HIDE) {
    debug("Window Hide: %d\n", wid);
    windows_window_hide(windows, wid);
  } else if (event == EVENT_WINDOW_CLOSE) {
    debug("Window Close: %d\n", wid);
    windows_window_destroy(windows, wid, 0);
  }
}

static void front_app_handler() {
  debug("Window Focus\n");
  schedule_focus_update(50000);
}

static void space_handler() {
  border_space_change_begin();
  // Not all native-fullscreen windows have yet updated their space id...
  schedule_space_update(20000);
}

void events_register(int cid) {
  void* cid_ctx = (void*)(intptr_t)cid;

  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_CLOSE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_MOVE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_RESIZE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_LEVEL, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UNHIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_HIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_TITLE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_REORDER, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UPDATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_CREATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_DESTROY, cid_ctx);

  SLSRegisterNotifyProc(space_handler, EVENT_SPACE_CHANGE, cid_ctx);

  SLSRegisterNotifyProc(front_app_handler, EVENT_FRONT_CHANGE, cid_ctx);

#ifdef DEBUG
  for (int i = 0; i < 2000; i++) {
    SLSRegisterNotifyProc(event_watcher, i, NULL);
  }
#endif
}
