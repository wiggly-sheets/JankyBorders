#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main borders_daemon_main
#include "../src/main.c"
#undef main

int main(void) {
  char valid[] = {
    'w','i','d','t','h','=','4','\0',
    'o','r','d','e','r','=','a','b','o','v','e','\0',
    '\0'
  };
  assert(message_payload_valid(valid, sizeof(valid)));

  struct settings parsed = {};
  uint32_t mask = parse_message_settings(&parsed, valid, sizeof(valid));
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert(parsed.border_width == 4.0f);
  assert(parsed.border_order == BORDER_ORDER_ABOVE);

  char missing_terminator[] = { 'w','i','d','t','h','=','4' };
  assert(!message_payload_valid(missing_terminator,
                                sizeof(missing_terminator)));

  char data_after_end[] = { 'w','i','d','t','h','=','4','\0','\0','x','\0' };
  assert(!message_payload_valid(data_after_end, sizeof(data_after_end)));
  char empty[] = { '\0' };
  assert(!message_payload_valid(empty, sizeof(empty)));
  assert(!message_payload_valid(NULL, 1));
  assert(!message_payload_valid(valid, 0));

  char* oversized = malloc(MACH_MAX_PAYLOAD_SIZE);
  assert(oversized);
  memset(oversized, 'x', MACH_MAX_PAYLOAD_SIZE - 1);
  oversized[MACH_MAX_PAYLOAD_SIZE - 1] = '\0';
  char* arguments[] = { "borders", oversized };
  assert(!send_args_to_server(1, 2, arguments));
  free(oversized);

  puts("bounded command payloads: ok");
  return 0;
}
