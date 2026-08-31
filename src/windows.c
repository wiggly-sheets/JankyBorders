#include "windows.h"
#include "hashtable.h"
#include "border.h"
#include "misc/ax.h"
#include <QuartzCore/QuartzCore.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <libproc.h>

extern pid_t g_pid;
extern struct settings g_settings;

// Loaded via dlsym in main.c
extern CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);

static bool windows_border_shimmer_enabled(struct border* border);

static bool window_in_list(struct table* list, char* app_name) {
  if (table_find(list, app_name)) return true;
  return false;
}

static bool app_allowed(struct settings* settings, char* app_name) {
  if (settings->whitelist_enabled
      && !window_in_list(&settings->whitelist, app_name)) {
    return false;
  }
  if (settings->blacklist_enabled
      && window_in_list(&settings->blacklist, app_name)) {
    return false;
  }
  return true;
}

static uint32_t windows_active_window_id(int cid) {
  return g_settings.ax_focus ? ax_get_front_window(cid) : get_front_window(cid);
}

static void windows_remove_all_except(struct table* windows, uint32_t wid) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket** bucket = &windows->buckets[i];
    while (*bucket) {
      struct bucket* current = *bucket;
      struct border* border = current->value;
      if (border && border->target_wid != wid) {
        *bucket = current->next;
        free(current->key);
        free(current);
        --windows->count;
        border_destroy(border);
      } else {
        bucket = &current->next;
      }
    }
  }
  windows_update_notifications(windows);
}

bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid) {
  bool window_created = false;
  int cid = SLSMainConnectionID();
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);

  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  static char pid_name_buffer[PROC_PIDPATHINFO_MAXSIZE];
  proc_name(pid, pid_name_buffer, sizeof(pid_name_buffer));

  if (!g_settings.enabled
      || pid == g_pid
      || g_settings.border_style == BORDER_STYLE_NONE
      || !app_allowed(&g_settings, pid_name_buffer)) return false;
  if (g_settings.active_only && wid != windows_active_window_id(cid)) return false;

  CFArrayRef target_ref = cfarray_of_cfnumbers(&wid,
                                               sizeof(uint32_t),
                                               1,
                                               kCFNumberSInt32Type);

  if (!target_ref) return false;

  CFTypeRef query = SLSWindowQueryWindows(cid, target_ref, 0x0);
  if (query) {
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (iterator && SLSWindowIteratorGetCount(iterator) > 0) {
      if (SLSWindowIteratorAdvance(iterator)) {
        if (window_suitable(iterator)) {
          struct border* border = table_find(windows, &wid);
          if (!border) {
            border = border_create();
            table_add(windows, &wid, border);
            window_created = true;
          }

          int32_t detected_radius = BORDER_DEFAULT_RADIUS;

          // Determine window corner radius
          if (JBSLSWindowIteratorGetCornerRadii) {
            CFArrayRef radii_ref = JBSLSWindowIteratorGetCornerRadii(iterator);
            if (radii_ref && CFArrayGetCount(radii_ref) > 0) {
              CFNumberRef value = CFArrayGetValueAtIndex(radii_ref, 0);
              if (!CFNumberGetValue(value,
                                    kCFNumberSInt32Type,
                                    &detected_radius)) {
                detected_radius = BORDER_DEFAULT_RADIUS;
              }
            }
            if (radii_ref) CFRelease(radii_ref);
          }
          border_set_detected_radius(border, detected_radius);
          border->target_wid = wid;
          border->sid = sid;
          if (g_settings.active_only) border->focused = true;
          border_update(border, false);
          if (windows_border_shimmer_enabled(border)) animation_start_ticker();
          windows_update_notifications(windows);
        }
      }
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
  }
  CFRelease(target_ref);

  return window_created;
}

static void windows_remove_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        border_destroy(border);
      }
      bucket = bucket->next;
    }
  }
  table_clear(windows);
  windows_update_notifications(windows);
}

void windows_recreate_all_borders(struct table* windows) {
  windows_remove_all(windows);
  windows_add_existing_windows(windows);
}

void windows_update_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_active(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && border->focused) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_inactive(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && !border->focused) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_window_update(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_update(border, true);
}

static void windows_cancel_border_animation(struct border* border) {
  border->animating = false;
  border->anim_mode = 0;
  border->anim_duration = 0.0f;
  border->anim_origin_override = false;
  border->anim_alpha = 1.0f;
}

static float windows_border_frame_offset(const struct settings* settings) {
  return settings->border_position == BORDER_POSITION_INSIDE
         ? 0.0f
         : -border_max_extent(settings) - BORDER_PADDING;
}

static bool windows_border_shimmer_enabled(struct border* border) {
  struct settings* settings = border_get_settings(border);
  return border->focused
         ? settings->shimmer_color_count >= 2
         : settings->inactive_shimmer_color_count >= 2;
}

static bool windows_window_focus_with_mouse_state(struct table* windows,
                                                  uint32_t wid,
                                                  bool mouse_down) {
  struct border* old_focus = NULL;
  struct border* new_focus = NULL;

  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border->focused) old_focus = border;
        if (border->target_wid == wid && !border->focused) new_focus = border;
      }
      bucket = bucket->next;
    }
  }

  struct settings* new_settings = new_focus
                                  ? border_get_settings(new_focus)
                                  : NULL;
  struct settings* old_settings = old_focus ? border_get_settings(old_focus) : NULL;
  if (new_settings
      && (new_settings->animation != 0
          || (old_settings && old_settings->inactive_animation != 0))
      && old_focus
      && !mouse_down) {
    CFTimeInterval now = CACurrentMediaTime();
    for (int i = 0; i < windows->capacity; ++i) {
      struct bucket* bucket = windows->buckets[i];
      while (bucket) {
        struct border* border = bucket->value;
        if (border && border->focused && border != new_focus) {
          windows_cancel_border_animation(border);
          border->focused = false;
          border->needs_redraw = true;
          if (old_settings->inactive_animation) {
            border->animating = true;
            border->anim_mode = old_settings->inactive_animation;
            border->anim_start = now;
            border->anim_duration = old_settings->animation_duration;
            border->anim_alpha = 1.0f;
          } else {
            border_update(border, false);
          }
        }
        bucket = bucket->next;
      }
    }

    windows_cancel_border_animation(new_focus);
    new_focus->focused = true;
    new_focus->animating = new_settings->animation != 0;
    new_focus->anim_mode = new_settings->animation;
    new_focus->anim_start = now;
    new_focus->anim_duration = new_settings->animation_duration;
    new_focus->anim_alpha = new_focus->animating ? 0.0f : 1.0f;

    if (new_settings->animation & ANIM_SLIDE) {
      CGRect old_bounds, new_bounds;
      CGError old_bounds_error = SLSGetWindowBounds(old_focus->cid,
                                                    old_focus->target_wid,
                                                    &old_bounds);
      CGError new_bounds_error = SLSGetWindowBounds(new_focus->cid,
                                                    new_focus->target_wid,
                                                    &new_bounds);
      if (old_bounds_error == kCGErrorSuccess
          && new_bounds_error == kCGErrorSuccess) {
        float old_offset = windows_border_frame_offset(old_settings);
        float new_offset = windows_border_frame_offset(new_settings);
        old_bounds = CGRectInset(old_bounds, old_offset, old_offset);
        new_bounds = CGRectInset(new_bounds, new_offset, new_offset);
        new_focus->anim_start_origin = old_bounds.origin;
        new_focus->anim_end_origin = new_bounds.origin;
        new_focus->anim_current_origin = new_focus->anim_start_origin;
        new_focus->anim_origin_override = true;
      }
    }

    new_focus->needs_redraw = true;
    if (!new_focus->animating) border_update(new_focus, false);
    animation_start_ticker();
    return true;
  }

  bool found_window = false;
  bool shimmer_enabled = false;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border->focused && border->target_wid != wid) {
          windows_cancel_border_animation(border);
          border->focused = false;
          border->needs_redraw = true;
          border_update(border, true);
        }

        if (!border->focused && border->target_wid == wid) {
          windows_cancel_border_animation(border);
          border->focused = true;
          border->needs_redraw = true;
          border_update(border, true);
        }

        if (border->target_wid == wid) found_window = true;
        if (windows_border_shimmer_enabled(border)) shimmer_enabled = true;
      }
      bucket = bucket->next;
    }
  }

  if (shimmer_enabled) animation_start_ticker();

  return found_window;
}

static bool windows_window_focus(struct table* windows, uint32_t wid) {
  bool mouse_down = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,
                                             kCGMouseButtonLeft);
  return windows_window_focus_with_mouse_state(windows, wid, mouse_down);
}

void windows_window_move(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_move(border);
}

void windows_window_hide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_hide(border);
}

void windows_window_unhide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_unhide(border);
}

bool windows_window_destroy(struct table* windows, uint32_t wid, uint32_t sid) {
  struct border* border = table_find(windows, &wid);
  if (border && (border->sid == sid || border->sticky || sid == 0)) {
    table_remove(windows, &wid);
    border_destroy(border);
    windows_update_notifications(windows);
    return true;
  }
  return false;
}

void windows_update_notifications(struct table* windows) {
  size_t window_count = 0;

  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket *bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value && window_count < INT_MAX) {
        ++window_count;
      } else if (bucket->value) {
        fprintf(stderr,
                "[!] Borders: Too many windows to register notifications\n");
        return;
      }
      bucket = bucket->next;
    }
  }

  uint32_t* window_list = NULL;
  size_t notification_count = 0;
  if (window_count > 0) {
    window_list = malloc(window_count * sizeof(uint32_t));
    if (!window_list) {
      fprintf(stderr,
              "[!] Borders: Failed to allocate window notification list\n");
      return;
    }

    for (int i = 0; i < windows->capacity; ++i) {
      struct bucket *bucket = windows->buckets[i];
      while (bucket) {
        if (bucket->value) {
          if (notification_count == window_count) {
            free(window_list);
            fprintf(stderr,
                    "[!] Borders: Window list changed while registering"
                    " notifications\n");
            return;
          }
          window_list[notification_count++] = *(uint32_t *)bucket->key;
        }
        bucket = bucket->next;
      }
    }
  }

  int cid = SLSMainConnectionID();
  SLSRequestNotificationsForWindows(cid,
                                    window_list,
                                    (int)notification_count);
  free(window_list);
}

void windows_determine_and_focus_active_window(struct table* windows) {
  int cid = SLSMainConnectionID();
  uint32_t front_wid = windows_active_window_id(cid);

  debug("Front window: %d\n", front_wid);
  if (!windows_window_focus(windows, front_wid)) {
    debug("Taking slow window focus path: %d\n", front_wid);
    if (front_wid && windows_window_create(windows,
                                           front_wid,
                                           window_space_id(cid, front_wid))) {
      windows_window_focus(windows, front_wid);
    }
  }
  if (g_settings.active_only) windows_remove_all_except(windows, front_wid);
}

void windows_draw_borders_on_current_spaces(struct table* windows) {
  debug("Space Change: Consistency check\n");
  int cid = SLSMainConnectionID();
  if (g_settings.active_only) {
    windows_determine_and_focus_active_window(windows);
    return;
  }

  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      if (border
          && !border->sticky
          && !is_space_visible(cid, border->sid)) {
        border_hide(border);
      }
      bucket = bucket->next;
    }
  }

  CFArrayRef displays = SLSCopyManagedDisplays(cid);
  uint32_t space_count = CFArrayGetCount(displays);
  uint64_t space_list[space_count];

  for (int i = 0; i < space_count; i++) {
    space_list[i] = SLSManagedDisplayGetCurrentSpace(cid,
                                          CFArrayGetValueAtIndex(displays, i));
  }

  CFRelease(displays);

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;
  CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(cid,
                                                            0,
                                                            space_list_ref,
                                                            0x2,
                                                            &set_tags,
                                                            &clear_tags    );

  if (window_list) {
    CFTypeRef query = SLSWindowQueryWindows(cid, window_list, 0x0);
    if (query) {
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
      if (iterator) {
        while(SLSWindowIteratorAdvance(iterator)) {
          if (window_suitable(iterator)) {
            uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
            struct border* border = table_find(windows, &wid);
            if (border) border_update(border, true);
            else {
              debug("Creating Missing Window: %d\n", wid);
              windows_window_create(windows, wid, window_space_id(cid, wid));
            }
          }
        }
        CFRelease(iterator);
      }
      CFRelease(query);
    }
    CFRelease(window_list);
  }
  CFRelease(space_list_ref);
}

void windows_cleanup_orphaned_borders(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      struct bucket* next = bucket->next;
      struct border* border = bucket->value;
      if (border && !window_is_valid(border->target_wid)) {
        debug("Cleaning up orphaned border for window: %d\n",
              border->target_wid);
        table_remove(windows, &border->target_wid);
        border_destroy(border);
      }
      bucket = next;
    }
  }
}

void windows_add_existing_windows(struct table* windows) {
  int cid = SLSMainConnectionID();
  if (g_settings.active_only) {
    windows_determine_and_focus_active_window(windows);
    return;
  }
  uint64_t* space_list = NULL;
  int space_count = 0;

  CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(cid);
  if (display_spaces_ref) {
    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
      CFDictionaryRef display_ref
                               = CFArrayGetValueAtIndex(display_spaces_ref, i);
      CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref,
                                                   CFSTR("Spaces"));
      int spaces_count = CFArrayGetCount(spaces_ref);

      space_list = (uint64_t*)realloc(space_list,
                                      sizeof(uint64_t)*(space_count
                                                        + spaces_count));

      for (int j = 0; j < spaces_count; ++j) {
        CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
        CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
        CFNumberGetValue(sid_ref,
                         CFNumberGetType(sid_ref),
                         space_list + space_count + j);
      }
      space_count += spaces_count;
    }
    CFRelease(display_spaces_ref);
  }

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);

  CFArrayRef window_list_ref = SLSCopyWindowsWithOptionsAndTags(cid,
                                                                0,
                                                                space_list_ref,
                                                                0x2,
                                                                &set_tags,
                                                                &clear_tags  );
  if (window_list_ref) {
    int count = CFArrayGetCount(window_list_ref);
    if (count > 0) {
      CFTypeRef query = SLSWindowQueryWindows(cid, window_list_ref, 0x0);
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);

      while (SLSWindowIteratorAdvance(iterator)) {
        if (window_suitable(iterator)) {
          uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
          windows_window_create(windows, wid, window_space_id(cid, wid));
        }
      }

      windows_update_notifications(windows);
      CFRelease(query);
      CFRelease(iterator);
    }
    CFRelease(window_list_ref);
  }
  CFRelease(space_list_ref);
  free(space_list);
}
