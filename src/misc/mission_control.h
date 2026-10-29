#pragma once

#include "../windows.h"
#include <libproc.h>

#define MISSION_CONTROL_OWNER          "WindowManager"
#define MISSION_CONTROL_MAX_BACKDROPS  16
#define MISSION_CONTROL_DEFAULT_BACKDROP_LEVEL 19
#define MISSION_CONTROL_EVENT_BURST_COUNT 4
#define MISSION_CONTROL_EVENT_BURST_SECONDS 0.25

extern bool g_mission_control_active;
extern struct table g_windows;

static uint32_t g_mc_backdrops[MISSION_CONTROL_MAX_BACKDROPS];
static int g_mc_backdrop_count = 0;
// Level 19 is the level used by the original upstream implementation, but it
// is not stable across macOS 27 builds. Learn it from the first credible
// WindowManager window that covers an entire active display, then require that
// level for any additional untracked backdrops.
static int g_mc_backdrop_level = MISSION_CONTROL_DEFAULT_BACKDROP_LEVEL;
static bool g_mc_backdrop_level_learned = false;
static uint32_t g_mc_level_candidates[MISSION_CONTROL_EVENT_BURST_COUNT];
static int g_mc_level_candidate_count = 0;
static CFAbsoluteTime g_mc_level_candidate_time = 0;

static int mission_control_find_backdrop(uint32_t wid);

static bool mission_control_is_backdrop(int cid, uint32_t wid) {
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);
  if (!wid_cid) {
    debug("MC candidate %u: no owner connection\n", wid);
    return false;
  }
  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  if (!pid) {
    debug("MC candidate %u: no owner pid\n", wid);
    return false;
  }

  char name[PROC_PIDPATHINFO_MAXSIZE] = {0};
  if (proc_name(pid, name, sizeof(name)) <= 0) {
    debug("MC candidate %u: unresolved owner pid %d\n", wid, pid);
    return false;
  }

  CGRect bounds = CGRectNull;
  SLSGetWindowBounds(cid, wid, &bounds);
  bool covers_display = mission_control_covers_display(bounds);

  int level = window_level(cid, wid);
  debug("MC candidate %u: owner=%s level=%d bounds=%.0f,%.0f %.0fx%.0f covers=%d\n",
        wid,
        name,
        level,
        bounds.origin.x,
        bounds.origin.y,
        bounds.size.width,
        bounds.size.height,
        covers_display);
  if (strcmp(name, MISSION_CONTROL_OWNER) != 0 || !covers_display) return false;
  int tracked_backdrop = mission_control_find_backdrop(wid);
  if (tracked_backdrop >= 0 || !g_mc_backdrop_level_learned) {
    g_mc_backdrop_level = level;
    g_mc_backdrop_level_learned = true;
  }
  if (level != g_mc_backdrop_level) return false;

  return true;
}

static int mission_control_find_backdrop(uint32_t wid) {
  for (int i = 0; i < g_mc_backdrop_count; ++i) {
    if (g_mc_backdrops[i] == wid) return i;
  }
  return -1;
}

static bool mission_control_window_manager_with_tiny_bounds(int cid,
                                                             uint32_t wid) {
  int wid_cid = 0;
  if (SLSGetWindowOwner(cid, wid, &wid_cid) != kCGErrorSuccess || !wid_cid) {
    return false;
  }
  pid_t pid = 0;
  if (SLSConnectionGetPID(wid_cid, &pid) != kCGErrorSuccess || !pid) {
    return false;
  }
  char name[PROC_PIDPATHINFO_MAXSIZE] = {0};
  if (proc_name(pid, name, sizeof(name)) <= 0
      || strcmp(name, MISSION_CONTROL_OWNER) != 0) {
    return false;
  }
  CGRect bounds = CGRectNull;
  if (SLSGetWindowBounds(cid, wid, &bounds) != kCGErrorSuccess) return false;
  return bounds.size.width <= 1.0 && bounds.size.height <= 1.0;
}

static void mission_control_enter() {
  if (g_mission_control_active) return;
  g_mission_control_active = true;
  debug("Mission Control Enter\n");
  windows_hide_all_borders(&g_windows);
}

void windows_mission_control_window_created(int cid, uint32_t wid) {
  if (mission_control_find_backdrop(wid) >= 0) return;
  if (g_mc_backdrop_count >= MISSION_CONTROL_MAX_BACKDROPS) return;
  if (!mission_control_is_backdrop(cid, wid)) return;

  g_mc_backdrops[g_mc_backdrop_count++] = wid;
  debug("Mission Control Enter (backdrop %d)\n", wid);
  mission_control_enter();
}

// On some macOS 27 builds WindowManager's Mission Control surfaces have zero
// bounds and never emit usable create events. They instead appear as four
// distinct level-change events in one short burst. Treat only that exact,
// owner-verified signature as a fallback; normal WindowManager UI does not
// satisfy both the geometry and burst constraints.
void windows_mission_control_window_level_changed(int cid, uint32_t wid) {
  windows_mission_control_window_created(cid, wid);
  if (g_mission_control_active
      || !mission_control_window_manager_with_tiny_bounds(cid, wid)) {
    return;
  }

  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  if (now - g_mc_level_candidate_time > MISSION_CONTROL_EVENT_BURST_SECONDS) {
    g_mc_level_candidate_count = 0;
  }
  g_mc_level_candidate_time = now;

  for (int i = 0; i < g_mc_level_candidate_count; ++i) {
    if (g_mc_level_candidates[i] == wid) return;
  }
  if (g_mc_level_candidate_count >= MISSION_CONTROL_EVENT_BURST_COUNT) return;
  g_mc_level_candidates[g_mc_level_candidate_count++] = wid;
  if (g_mc_level_candidate_count < MISSION_CONTROL_EVENT_BURST_COUNT) return;

  for (int i = 0; i < g_mc_level_candidate_count; ++i) {
    g_mc_backdrops[g_mc_backdrop_count++] = g_mc_level_candidates[i];
  }
  g_mc_level_candidate_count = 0;
  debug("Mission Control Enter (WindowManager event burst)\n");
  mission_control_enter();
}

void windows_mission_control_window_destroyed(int cid, uint32_t wid) {
  if (mission_control_find_backdrop(wid) < 0) return;

  // SkyLight also emits destroy while a backdrop moves between spaces. Only
  // remove it after verifying that the underlying window really no longer
  // exists.
  windows_mission_control_prune(cid);
}

static void mission_control_exit() {
  g_mission_control_active = false;
  debug("Mission Control Exit\n");
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50ull * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    windows_draw_borders_on_current_spaces(&g_windows);
    windows_determine_and_focus_active_window(&g_windows);
  });
}

void windows_mission_control_prune(int cid) {
  if (!g_mission_control_active || !g_mc_backdrop_count) return;

  int kept = 0;
  for (int i = 0; i < g_mc_backdrop_count; ++i) {
    uint32_t wid = g_mc_backdrops[i];
    int wid_cid = 0;
    if (SLSGetWindowOwner(cid, wid, &wid_cid) == kCGErrorSuccess && wid_cid) {
      g_mc_backdrops[kept++] = wid;
    }
  }
  g_mc_backdrop_count = kept;
  if (g_mc_backdrop_count) return;

  mission_control_exit();
}
