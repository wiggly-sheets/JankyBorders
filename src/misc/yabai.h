#pragma once
#include <dispatch/dispatch.h>
#define _YABAI_INTEGRATION

#ifdef _YABAI_INTEGRATION
#include "extern.h"
#include "../windows.h"
#include "../mach.h"
#include <CoreVideo/CoreVideo.h>
#include <math.h>
#include <pthread.h>

// Additional border interfaces needed for the yabai integration
void border_init(struct border* border, int cid);
void border_create_window(struct border* border, CGRect frame, bool unmanaged, bool hidpi);
void border_destroy_proxy(struct border* proxy);
void border_update_internal(struct border* border, struct settings* settings);

struct track_transform_payload {
  int cid;
  uint32_t border_wid;
  uint32_t proxy_wid;
  uint32_t target_wid;
  CGAffineTransform initial_transform;
};

struct yabai_proxy_payload {
  union { struct border* proxy; struct border* border; };
  struct settings settings;
  uint32_t border_wid;
  uint32_t background_wid;
  uint32_t real_wid;
  uint32_t external_proxy_wid;
  CGRect proxy_frame;
};

static inline bool yabai_bounds_are_valid(CGRect bounds) {
  return isfinite(bounds.origin.x)
         && isfinite(bounds.origin.y)
         && isfinite(bounds.size.width)
         && isfinite(bounds.size.height)
         && bounds.size.width > 0.0
         && bounds.size.height > 0.0;
}

static inline void yabai_destroy_receive_port(ipc_space_t task,
                                              mach_port_t port) {
  if (port != MACH_PORT_NULL) {
    mach_port_mod_refs(task, port, MACH_PORT_RIGHT_RECEIVE, -1);
  }
}

static CVReturn track_transform(CVDisplayLinkRef display_link, const CVTimeStamp* now, const CVTimeStamp* output_time, CVOptionFlags flags, CVOptionFlags* flags_out, void* context) {
  struct animation* animation = context;
  usleep(0.25*animation->frame_time);

  struct track_transform_payload* payload = animation->context;
  CGAffineTransform target_transform, border_transform;
  CGError error = SLSGetWindowTransform(payload->cid,
                                        payload->target_wid,
                                        &target_transform   );

  if (error != kCGErrorSuccess) return kCVReturnSuccess;

  border_transform = CGAffineTransformConcat(target_transform,
                                             payload->initial_transform);

  CFTypeRef transaction = SLSTransactionCreate(payload->cid);
  if (transaction) {
    SLSTransactionSetWindowTransform(transaction, payload->proxy_wid, 0, 0, border_transform);
    SLSTransactionSetWindowTransform(transaction, payload->border_wid, 0, 0, border_transform);
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
  }
  return kCVReturnSuccess;
}

static void* yabai_proxy_begin_proc(void* context) {
  struct yabai_proxy_payload* info = context;
  struct border* proxy = info->proxy;
  pthread_mutex_lock(&proxy->mutex);
  if (proxy->is_destroyed
      || !yabai_bounds_are_valid(proxy->target_bounds)
      || !yabai_bounds_are_valid(info->proxy_frame)) {
    pthread_mutex_unlock(&proxy->mutex);
    settings_destroy(&info->settings);
    free(context);
    return NULL;
  }

  struct track_transform_payload* payload
                            = malloc(sizeof(struct track_transform_payload));
  if (!payload) {
    pthread_mutex_unlock(&proxy->mutex);
    settings_destroy(&info->settings);
    free(context);
    return NULL;
  }

  payload->proxy_wid = proxy->wid;
  payload->border_wid = info->border_wid;
  payload->target_wid = info->external_proxy_wid;
  payload->cid = proxy->cid;

  payload->initial_transform = CGAffineTransformIdentity;
  payload->initial_transform.a = proxy->target_bounds.size.width
                                / info->proxy_frame.size.width;
  payload->initial_transform.d = proxy->target_bounds.size.height
                                / info->proxy_frame.size.height;
  payload->initial_transform.tx = 0.5*(proxy->frame.size.width
                                 - proxy->target_bounds.size.width);
  payload->initial_transform.ty = 0.5*(proxy->frame.size.height
                                 - proxy->target_bounds.size.height);

  animation_stop(&proxy->animation);
  animation_start(&proxy->animation, track_transform, payload);

  proxy->frame = CGRectNull;
  border_update_internal(proxy, &info->settings);

  CFTypeRef transaction = SLSTransactionCreate(proxy->cid);
  if (transaction) {
    SLSTransactionOrderWindow(transaction,
                              proxy->wid,
                              border_effective_order(&info->settings),
                              info->external_proxy_wid    );

    SLSTransactionSetWindowAlpha(transaction, info->border_wid, 0.f);
    if (info->background_wid) {
      SLSTransactionSetWindowAlpha(transaction, info->background_wid, 0.f);
    }
    SLSTransactionSetWindowAlpha(transaction, proxy->wid, 1.f);
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
  }

  pthread_mutex_unlock(&proxy->mutex);
  settings_destroy(&info->settings);
  free(context);
  return NULL;
}

static void* yabai_proxy_end_proc(void* context) {
  struct yabai_proxy_payload* info = context;
  struct border* border = info->border;
  pthread_mutex_lock(&border->mutex);
  border->event_buffer.disable_coalescing = true;
  border->external_proxy_wid = 0;
  border_update_internal(border, &info->settings);
  border->event_buffer.disable_coalescing = false;
  pthread_mutex_unlock(&border->mutex);
  settings_destroy(&info->settings);
  free(context);
  return NULL;
}

static inline void yabai_proxy_begin(struct table* windows, uint32_t wid, uint32_t real_wid) {
  if (!real_wid || !wid) return;
  struct border* border = table_find(windows, &real_wid);

  if (border) {
    pthread_mutex_lock(&border->mutex);
    CGRect proxy_frame = CGRectNull;
    if (border->is_destroyed
        || SLSGetWindowBounds(border->cid,
                              wid,
                              &proxy_frame) != kCGErrorSuccess
        || !yabai_bounds_are_valid(proxy_frame)) {
      pthread_mutex_unlock(&border->mutex);
      return;
    }
    border->external_proxy_wid = wid;
    if (!border->proxy) {
      border->proxy = malloc(sizeof(struct border));
      if (!border->proxy) {
        border->external_proxy_wid = 0;
        pthread_mutex_unlock(&border->mutex);
        return;
      }
      border_init(border->proxy, border->cid);
      border->proxy->is_proxy = true;
      border->proxy->target_bounds = border->target_bounds;
      border->proxy->frame = border->frame;
      border->proxy->focused = border->focused;
      border->proxy->target_wid = border->target_wid;
      border->proxy->sid = border->sid;
      border->proxy->radius = border->radius;
      border->proxy->inner_radius = border->inner_radius;
      border_create_window(border->proxy, CGRectNull, true, false);
    }

    struct yabai_proxy_payload* payload
                            = malloc(sizeof(struct yabai_proxy_payload));
    if (!payload) {
      border->external_proxy_wid = 0;
      pthread_mutex_unlock(&border->mutex);
      return;
    }
    payload->proxy = border->proxy;
    payload->border_wid = border->wid;
    payload->background_wid = border->background_wid;
    payload->external_proxy_wid = border->external_proxy_wid;
    payload->real_wid = real_wid;
    payload->proxy_frame = proxy_frame;
    settings_snapshot(&payload->settings, border_get_settings(border));
    pthread_mutex_unlock(&border->mutex);
    yabai_proxy_begin_proc(payload);
  }
}

static inline void yabai_proxy_end(struct table* windows, uint32_t wid, uint32_t real_wid) {
  if (!real_wid || !wid) return;
  struct border* border = (struct border*)table_find(windows, &real_wid);
  struct yabai_proxy_payload* payload = NULL;
  if (border) pthread_mutex_lock(&border->mutex);
  if (border
      && !border->is_destroyed
      && border->proxy
      && border->external_proxy_wid == wid) {
    struct border* proxy = border->proxy;
    border->proxy = NULL;

    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      SLSTransactionSetWindowAlpha(transaction, proxy->wid, 0.f);
      SLSTransactionSetWindowAlpha(transaction, border->wid, 1.f);
      if (border->background_wid) {
        SLSTransactionSetWindowAlpha(transaction,
                                     border->background_wid,
                                     1.f);
      }
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }

    border_destroy_proxy(proxy);

    payload = malloc(sizeof(struct yabai_proxy_payload));
    if (payload) {
      payload->border = border;
      payload->border_wid = border->wid;
      payload->background_wid = border->background_wid;
      settings_snapshot(&payload->settings, border_get_settings(border));
    } else {
      border->external_proxy_wid = 0;
      border_update_internal(border, border_get_settings(border));
    }
  }
  if (border) pthread_mutex_unlock(&border->mutex);
  if (payload) yabai_proxy_end_proc(payload);
}

static void yabai_message(CFMachPortRef port, void* data, CFIndex size, void* context) {
  (void)port;
  if (!data || size < (CFIndex)sizeof(mach_msg_header_t)) return;

  struct mach_message* message = data;
  bool destroyable = message->header.msgh_size >= sizeof(mach_msg_header_t)
                     && message->header.msgh_size <= (mach_msg_size_t)size;
  struct yabai_payload {
    uint32_t event;
    uint32_t count;
    uint32_t proxy_wid[512];
    uint32_t real_wid[512];
  };

  void* descriptor_payload = NULL;
  uint32_t descriptor_size = 0;
  if (mach_message_get_payload(data,
                               (size_t)size,
                               &descriptor_payload,
                               &descriptor_size)
      && descriptor_size == sizeof(struct yabai_payload)) {
    struct yabai_payload* payload = descriptor_payload;
    if (payload->count <= 512) {
      if (payload->event == 1325) {
        for (uint32_t i = 0; i < payload->count; ++i) {
          yabai_proxy_begin(context,
                            payload->proxy_wid[i],
                            payload->real_wid[i]);
        }
      } else if (payload->event == 1326) {
        for (uint32_t i = 0; i < payload->count; ++i) {
          yabai_proxy_end(context,
                          payload->proxy_wid[i],
                          payload->real_wid[i]);
        }
      }
    }
  }
  if (destroyable) mach_msg_destroy(&message->header);
}

static inline void yabai_register_mach_port(struct table* windows) {
  ipc_space_t task = mach_task_self();
  mach_port_t port;
  if (mach_port_allocate(task,
                         MACH_PORT_RIGHT_RECEIVE,
                         &port                   ) != KERN_SUCCESS) {
    return;
  }

  struct mach_port_limits limits = { 1 };
  if (mach_port_set_attributes(task,
                               port,
                               MACH_PORT_LIMITS_INFO,
                               (mach_port_info_t)&limits,
                               MACH_PORT_LIMITS_INFO_COUNT) != KERN_SUCCESS) {
    yabai_destroy_receive_port(task, port);
    return;
  }

  if (!mach_register_port(port, "git.felix.jbevent")) {
    yabai_destroy_receive_port(task, port);
    return;
  }

  CFMachPortContext context = { .info = (void*)windows };

  CFMachPortRef cf_mach_port = CFMachPortCreateWithPort(NULL,
                                                        port,
                                                        yabai_message,
                                                        &context,
                                                        false         );
  if (!cf_mach_port) {
    yabai_destroy_receive_port(task, port);
    return;
  }

  CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(NULL,
                                                            cf_mach_port,
                                                            0            );
  if (!source) {
    CFRelease(cf_mach_port);
    yabai_destroy_receive_port(task, port);
    return;
  }

  CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
  CFRelease(source);
  CFRelease(cf_mach_port);
}
#endif
