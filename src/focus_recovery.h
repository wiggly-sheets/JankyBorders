#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pure decision logic for focus recovery. No I/O, no macOS dependency, so it
// is testable in isolation and safe to call from any border path.

enum { FOCUS_RECOVERY_RETRY_COUNT = 4 };

// WindowServer publishes the front process, the active display and the window
// order as separate updates, so a focus change only becomes trustworthy a few
// hundred microseconds in. Keep the series short and bounded, and share it with
// the tests so the recovery window cannot grow by accident.
static const uint64_t focus_recovery_retry_delays_us[
    FOCUS_RECOVERY_RETRY_COUNT] = {
  10000, 60000, 160000, 350000,
};

enum focus_recovery_resolution {
  FOCUS_RECOVERY_RESULT_RESOLVED,
  FOCUS_RECOVERY_RESULT_TRANSIENT,
  FOCUS_RECOVERY_RESULT_COMPLETE_NONE,
  FOCUS_RECOVERY_RESULT_AMBIGUOUS,
};

enum focus_recovery_decision {
  FOCUS_RECOVERY_SELECT,
  FOCUS_RECOVERY_HOLD,
  FOCUS_RECOVERY_CLEAR,
};

// A focus change arrives as a burst of notifications. One series runs at a
// time; events that land mid-series only mark it dirty and are drained once at
// the end, so a burst can never postpone the work indefinitely.
struct focus_recovery_scheduler {
  bool active;
  bool dirty;
};

// Only two adjacent identical probes prove the frontmost window settled: a
// single probe agrees just as readily with a stale window order, so any probe
// without a trustworthy hint breaks the sequence instead of confirming it.
struct focus_recovery_fallback_snapshot {
  int front_cid;
  uint32_t wid;
};

static inline void focus_recovery_fallback_reset(
    struct focus_recovery_fallback_snapshot* snapshot) {
  if (!snapshot) return;
  snapshot->front_cid = 0;
  snapshot->wid = 0;
}

static inline bool focus_recovery_fallback_observe(
    struct focus_recovery_fallback_snapshot* snapshot,
    int front_cid,
    uint32_t wid) {
  if (!snapshot) return false;
  if (front_cid <= 0 || !wid) {
    focus_recovery_fallback_reset(snapshot);
    return false;
  }
  if (snapshot->front_cid == front_cid && snapshot->wid == wid) {
    return true;
  }
  snapshot->front_cid = front_cid;
  snapshot->wid = wid;
  return false;
}

static inline bool focus_recovery_scheduler_schedule(
    struct focus_recovery_scheduler* scheduler) {
  if (!scheduler) return false;
  if (scheduler->active) {
    scheduler->dirty = true;
    return false;
  }
  scheduler->active = true;
  scheduler->dirty = false;
  return true;
}

static inline bool focus_recovery_scheduler_observe_events(
    struct focus_recovery_scheduler* scheduler) {
  if (!scheduler) return false;
  bool dirty = scheduler->dirty;
  scheduler->dirty = false;
  return dirty;
}

static inline bool focus_recovery_scheduler_finish(
    struct focus_recovery_scheduler* scheduler,
    bool held,
    bool observed_new_event) {
  if (!scheduler) return false;
  scheduler->active = false;
  scheduler->dirty = false;
  return held && observed_new_event;
}

static inline void focus_recovery_scheduler_cancel(
    struct focus_recovery_scheduler* scheduler) {
  if (!scheduler) return;
  scheduler->active = false;
  scheduler->dirty = false;
}

// Only the last attempt of the series may act on a verdict that tears the
// border down. Earlier attempts report TRANSIENT so a late WindowServer update
// still gets the chance to name a real window first.
static inline enum focus_recovery_resolution focus_recovery_gate_clear(
    enum focus_recovery_resolution resolution,
    bool allow_clear) {
  if (!allow_clear && resolution != FOCUS_RECOVERY_RESULT_RESOLVED) {
    return FOCUS_RECOVERY_RESULT_TRANSIENT;
  }
  return resolution;
}

static inline bool focus_recovery_retry_is_current(
    uint64_t current_generation,
    uint64_t scheduled_generation) {
  return current_generation == scheduled_generation;
}

// The candidate list arrives in our own order, so the frontmost match has to
// come from the window order rather than from the candidate order.
static inline uint32_t focus_recovery_select_frontmost(
    const uint32_t* ordered_windows,
    size_t ordered_count,
    const uint32_t* candidates,
    size_t candidate_count) {
  if (!ordered_windows || !candidates) return 0;

  for (size_t ordered_index = 0; ordered_index < ordered_count;
       ++ordered_index) {
    uint32_t wid = ordered_windows[ordered_index];
    if (!wid) continue;
    for (size_t candidate_index = 0; candidate_index < candidate_count;
         ++candidate_index) {
      if (candidates[candidate_index] == wid) return wid;
    }
  }

  return 0;
}

static inline enum focus_recovery_decision focus_recovery_decide(
    enum focus_recovery_resolution resolution,
    bool previous_target_matches_front_process) {
  switch (resolution) {
    case FOCUS_RECOVERY_RESULT_RESOLVED:
      return FOCUS_RECOVERY_SELECT;
    case FOCUS_RECOVERY_RESULT_TRANSIENT:
      return FOCUS_RECOVERY_HOLD;
    case FOCUS_RECOVERY_RESULT_COMPLETE_NONE:
      return FOCUS_RECOVERY_CLEAR;
    case FOCUS_RECOVERY_RESULT_AMBIGUOUS:
      // Preserve a confirmed window while signals for the same application
      // settle. Once the front process has definitely changed, keeping the old
      // application's border would be a false focus indicator.
      return previous_target_matches_front_process
                 ? FOCUS_RECOVERY_HOLD
                 : FOCUS_RECOVERY_CLEAR;
  }

  // An unrecognised resolver state is a signal we failed to read, not a
  // signal that the border should go away.
  return FOCUS_RECOVERY_HOLD;
}