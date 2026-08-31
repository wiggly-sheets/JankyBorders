#include <assert.h>
#include <stdio.h>

#include "../src/misc/window.h"

static uint64_t received_sid;
static int move_count;

CGError SLSMoveWindowsToManagedSpace(int cid,
                                     CFArrayRef window_list,
                                     uint64_t sid) {
  (void)cid;
  assert(window_list);
  received_sid = sid;
  move_count++;
  return kCGErrorSuccess;
}

int main(void) {
  uint64_t wide_sid = UINT64_C(0x100000001);
  window_send_to_space(1, 42, wide_sid);
  assert(move_count == 1);
  assert(received_sid == wide_sid);

  window_send_to_space(1, 0, wide_sid);
  window_send_to_space(1, 42, 0);
  assert(move_count == 1);

  puts("64-bit Space ID forwarding: ok");
  return 0;
}
