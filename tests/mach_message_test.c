#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/mach.h"

static struct mach_message valid_message(void* payload, uint32_t payload_size) {
  struct mach_message message = { 0 };
  message.header.msgh_bits = MACH_MSGH_BITS_COMPLEX;
  message.header.msgh_size = sizeof(message);
  message.msgh_descriptor_count = 1;
  message.descriptor.address = payload;
  message.descriptor.size = payload_size;
  message.descriptor.type = MACH_MSG_OOL_DESCRIPTOR;
  return message;
}

int main(void) {
  char bytes[] = "argument";
  struct mach_message message = valid_message(bytes, sizeof(bytes));
  void* payload = NULL;
  uint32_t payload_size = 0;

  assert(mach_message_get_payload(&message,
                                  sizeof(message),
                                  &payload,
                                  &payload_size));
  assert(payload == bytes);
  assert(payload_size == sizeof(bytes));

  assert(!mach_message_get_payload(NULL,
                                   sizeof(message),
                                   &payload,
                                   &payload_size));
  assert(!mach_message_get_payload(&message,
                                   sizeof(mach_msg_header_t),
                                   &payload,
                                   &payload_size));

  struct mach_message invalid = message;
  invalid.header.msgh_size = sizeof(invalid) - 1;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  invalid = message;
  invalid.header.msgh_bits = 0;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  invalid = message;
  invalid.msgh_descriptor_count = 2;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  invalid = message;
  invalid.descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  invalid = message;
  invalid.descriptor.address = NULL;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  invalid = message;
  invalid.descriptor.size = 0;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  invalid = message;
  invalid.descriptor.size = MACH_MAX_PAYLOAD_SIZE + 1;
  assert(!mach_message_get_payload(&invalid,
                                   sizeof(invalid),
                                   &payload,
                                   &payload_size));

  puts("mach message validation: ok");
  return 0;
}
