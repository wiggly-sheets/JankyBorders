#include "border.h"
#include "hashtable.h"
#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "mach.h"
#include "parse.h"
#include "misc/connection.h"
#include "misc/ax.h"
#include "misc/yabai.h"
#include "animation.h"
#include <stdio.h>
#include <dlfcn.h>

#define VERSION_OPT_LONG "--version"
#define VERSION_OPT_SHRT "-v"

#define HELP_OPT_LONG "--help"
#define HELP_OPT_SHRT "-h"

#define MAJOR 1
#define MINOR 9
#define PATCH 0

// Resolved via dlsym because of availability
CFArrayRef (* JBSLSWindowIteratorGetCornerRadii)(CFTypeRef) = NULL;

pid_t g_pid;
mach_port_t g_server_port;
struct table g_windows;
struct mach_server g_mach_server;
struct settings g_settings = { .enabled = true,
                               .active_window = {
                                 .layer_count = 1,
                                 .layers = {{ .stype = COLOR_STYLE_SOLID,
                                              .color = 0xffe1e3e4 }}},
                               .inactive_window = {
                                 .layer_count = 1,
                                 .layers = {{ .stype = COLOR_STYLE_SOLID,
                                              .color =  0x00000000 }}},
                               .background = { .stype = COLOR_STYLE_SOLID,
                                               .color = 0x00000000         },
                               .border_width = 4.f,
                               .inner_border_width = 4.f,
                               .double_border_gap = 0.f,
                               .blur_radius = 0,
                               .border_style = BORDER_STYLE_ROUND,
                               .hidpi = false,
                               .background_mode = BORDER_BACKGROUND_AUTO,
                               .border_order = BORDER_ORDER_BELOW,
                               .ax_focus = false,
                               .animation = 0,
                               .inactive_animation = 0,
                               .animation_duration = 0.25f,
                               .animation_easing = ANIMATION_EASING_LINEAR,
                               .shimmer_duration = 3.0f,
                               .shimmer_fps = 30.0f,
                               .blacklist_enabled = false,
                               .whitelist_enabled = false                    };

static TABLE_HASH_FUNC(hash_windows) {
  return *(uint32_t *) key;
}

static TABLE_COMPARE_FUNC(cmp_windows) {
  return *(uint32_t *) key_a == *(uint32_t *) key_b;
}

static TABLE_HASH_FUNC(hash_blacklist) {
  // djb2 by Dan Bernstein
  unsigned long hash = 5381;
  char c;
  while((c = *((char*)key++))) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static TABLE_COMPARE_FUNC(cmp_blacklist) {
  return strcmp((char*)key_a, (char*)key_b) == 0;
}

static bool message_payload_valid(const void* data, uint32_t len) {
  if (!data || !len || len > MACH_MAX_PAYLOAD_SIZE) return false;

  const char* bytes = data;
  uint32_t offset = 0;
  bool found_argument = false;
  while (offset < len) {
    const char* terminator = memchr(bytes + offset, '\0', len - offset);
    if (!terminator) return false;

    size_t argument_length = (size_t)(terminator - (bytes + offset));
    offset += (uint32_t)argument_length + 1;
    if (!argument_length) {
      while (offset < len) {
        if (bytes[offset++] != '\0') return false;
      }
      break;
    }
    found_argument = true;
  }
  return found_argument;
}

static uint32_t parse_message_settings(struct settings* settings,
                                       void* data,
                                       uint32_t len) {
  char* bytes = data;
  uint32_t offset = 0;
  uint32_t update_mask = 0;
  while (offset < len && bytes[offset]) {
    char* argument = bytes + offset;
    update_mask |= parse_settings(settings, 1, &argument);
    offset += (uint32_t)strlen(argument) + 1;
  }
  return update_mask;
}

static void message_handler(void* data, uint32_t len) {
  if (!message_payload_valid(data, len)) {
    printf("[?] Borders: Ignoring malformed command message\n");
    return;
  }

  struct settings settings;
  settings_clone(&settings, &g_settings);
  uint32_t update_mask = parse_message_settings(&settings, data, len);

  if (settings.apply_to > 0) {
    struct border* border = table_find(&g_windows, &settings.apply_to);
    if (border) {
      if (update_mask & BORDER_UPDATE_MASK_WINDOW_STATE) {
        border->window_state = settings.window_state;
      }
      if (update_mask & ~BORDER_UPDATE_MASK_WINDOW_STATE) {
        settings_replace(&border->setting_override, &settings);
        border->setting_override.enabled = true;
      }
      border->needs_redraw = true;
      border_update(border, true);
      if (settings_shimmer_enabled(&border->setting_override)) {
        animation_start_ticker();
      }
    }
    settings_destroy(&settings);
    return;
  } else {
    settings_move(&g_settings, &settings);
    for (int i = 0; i < g_windows.capacity; ++i) {
      struct bucket* bucket = g_windows.buckets[i];
      while (bucket) {
        if (bucket->value) {
          struct border* border = bucket->value;
          if (border->setting_override.enabled) {
            uint32_t window_update_mask = parse_message_settings(
                &border->setting_override, data, len);

            if (window_update_mask
                && !((update_mask & BORDER_UPDATE_MASK_ALL)
                     || (update_mask & BORDER_UPDATE_MASK_RECREATE_ALL))) {
              border->needs_redraw = true;
              border_update(border, true);
            }
          }
        }
        bucket = bucket->next;
      }
    }
  }

  if (update_mask & BORDER_UPDATE_MASK_RECREATE_ALL) {
    windows_recreate_all_borders(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_ALL) {
    windows_update_all(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_ACTIVE) {
    windows_update_active(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_INACTIVE) {
    windows_update_inactive(&g_windows);
  }
  if (settings_shimmer_enabled(&g_settings)) animation_start_ticker();
}

static bool send_args_to_server(mach_port_t port, int argc, char** argv) {
  if (!port || argc <= 1 || !argv) return false;
  size_t message_length = 1;
  for (int i = 1; i < argc; i++) {
    if (!argv[i]) return false;
    size_t argument_length = strlen(argv[i]);
    if (argument_length >= MACH_MAX_PAYLOAD_SIZE
        || message_length > MACH_MAX_PAYLOAD_SIZE - argument_length - 1) {
      return false;
    }
    message_length += argument_length + 1;
  }

  char* message = malloc(message_length);
  if (!message) return false;
  char* temp = message;
  for (int i = 1; i < argc; i++) {
    size_t argument_length = strlen(argv[i]) + 1;
    memcpy(temp, argv[i], argument_length);
    temp += argument_length;
  }
  *temp++ = '\0';

  mach_send_message(port, message, (uint32_t)message_length);
  free(message);
  return true;
}

static void event_callback(CFMachPortRef port, void* message, CFIndex size, void* context) {
  int cid = SLSMainConnectionID();
  CGEventRef event = SLEventCreateNextEvent(cid);
  if (!event) return;
  do {
    CFRelease(event);
    event = SLEventCreateNextEvent(cid);
  } while (event);
}

void load_symbols() {
  if (__builtin_available(macOS 26.0, *)) {
    void* lib = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY | RTLD_LOCAL);
    if (lib) {
      JBSLSWindowIteratorGetCornerRadii = dlsym(lib, "SLSWindowIteratorGetCornerRadii");
    }
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && ((strcmp(argv[1], VERSION_OPT_LONG) == 0)
                   || (strcmp(argv[1], VERSION_OPT_SHRT) == 0))) {
    fprintf(stdout, "borders-v%d.%d.%d\n", MAJOR, MINOR, PATCH);
    exit(EXIT_SUCCESS);
  }

  if (argc > 1 && ((strcmp(argv[1], HELP_OPT_LONG) == 0)
                   || (strcmp(argv[1], HELP_OPT_SHRT) == 0))) {
    fprintf(stdout, "Refer to the man page for help: man borders\n");
    exit(EXIT_SUCCESS);
  }

  settings_init_filter_tables(&g_settings, 64, hash_blacklist, cmp_blacklist);
  g_settings.ax_focus = ax_check_trust(true);

  uint32_t update_mask = parse_settings(&g_settings, argc - 1, argv + 1);
  mach_port_t server_port = mach_get_bs_port(BS_NAME);
  if (server_port && update_mask) {
    if (!send_args_to_server(server_port, argc, argv)) {
      error("Failed to send border settings: command payload is too large.\n");
    }
    return 0;
  } else if (server_port) {
    error("A borders instance is already running and no valid arguments"
          " where provided. To modify properties of the running instance"
          " provide them as arguments.\n");
  }

  load_symbols();
  pid_for_task(mach_task_self(), &g_pid);
  table_init(&g_windows, 1024, hash_windows, cmp_windows);

  g_server_port = create_connection_server_port();

  int cid = SLSMainConnectionID();
  events_register(cid);

  mach_port_t port;
  CGError err = SLSGetEventPort(cid, &port);
  if (err == kCGErrorSuccess) {
    CFMachPortRef cf_mach_port = CFMachPortCreateWithPort(NULL,
                                                          port,
                                                          event_callback,
                                                          NULL,
                                                          false          );
    if (!cf_mach_port) error("Failed to create the WindowServer event port.\n");

    _CFMachPortSetOptions(cf_mach_port, 0x40);
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL,
                                                              cf_mach_port,
                                                              0            );
    if (!source) {
      CFRelease(cf_mach_port);
      error("Failed to create the WindowServer event source.\n");
    }

    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    CFRelease(cf_mach_port);
    CFRelease(source);
  } else {
    error("Failed to get the WindowServer event port.\n");
  }

  windows_add_existing_windows(&g_windows);

  if (!mach_server_begin(&g_mach_server, message_handler)) {
    error("Failed to start the borders command server.\n");
  }
  if (!update_mask) execute_config_file("borders", "bordersrc");
  // Startup configuration bypasses message_handler(), which is normally
  // responsible for starting the redraw timer after enabling shimmer.
  if (settings_shimmer_enabled(&g_settings)) animation_start_ticker();

  #ifdef _YABAI_INTEGRATION
  yabai_register_mach_port(&g_windows);
  #endif

  dispatch_source_t cleanup_timer = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(cleanup_timer,
                            dispatch_time(DISPATCH_TIME_NOW,
                                          60ull * NSEC_PER_SEC),
                            60ull * NSEC_PER_SEC,
                            10ull * NSEC_PER_SEC);
  dispatch_source_set_event_handler(cleanup_timer, ^{
    windows_cleanup_orphaned_borders(&g_windows);
  });
  dispatch_resume(cleanup_timer);

  CFRunLoopRun();
  return 0;
}
