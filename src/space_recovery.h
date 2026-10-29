#pragma once
#include <stdbool.h>
#include <stdint.h>

// Pure predicates for space-change recovery. No I/O, no macOS dependency, so
// they are testable in isolation and safe to call from any border path.

enum { SPACE_CHANGE_RETRY_COUNT = 5 };

static const uint64_t space_change_retry_delays_us[SPACE_CHANGE_RETRY_COUNT] = {
    20000, 100000, 250000, 500000, 1000000,
};

// A queued retry only matters while the generation it was scheduled for is
// still the current one; anything else is a stale timer from a superseded
// change and must be dropped instead of migrated.
static inline bool space_change_retry_is_current(uint64_t current_generation,
                                                 uint64_t scheduled_generation) {
  return current_generation == scheduled_generation;
}

// The helper window only follows the border once we know a real target space
// and the helper is not already there. `retry` re-drives the move after a
// failed attempt without requiring the target to have changed again.
static inline bool border_space_should_migrate(uint64_t previous_target_sid,
                                               uint64_t target_sid,
                                               uint64_t helper_sid,
                                               bool retry) {
  return target_sid != 0 && helper_sid != target_sid &&
         (target_sid != previous_target_sid || retry);
}

// Proxies and sticky windows never wait on a space move. Everything else may
// take focus only once the target space is known and the helper has actually
// landed there, so we never raise a border window into the wrong space.
static inline bool border_space_ready_for_focus(bool is_proxy,
                                                bool sticky,
                                                bool target_space_known,
                                                uint64_t target_sid,
                                                uint64_t helper_sid) {
  return is_proxy || sticky ||
         (target_space_known && target_sid != 0 && helper_sid == target_sid);
}