#include "animation.h"
#include <CoreFoundation/CoreFoundation.h>
#include <QuartzCore/QuartzCore.h>
#include <math.h>
#include "border.h"
#include "hashtable.h"

extern struct table g_windows;

#define ANIMATION_FPS 60.0
#define SHIMMER_MAX_FPS 120.0

static CFRunLoopTimerRef g_anim_timer = NULL;

static bool animation_border_shimmer_enabled(
    const struct border* border,
    const struct settings* settings) {
  return border->focused
         ? settings->shimmer_color_count >= 2
         : settings->inactive_shimmer_color_count >= 2;
}

static double animation_required_fps(void) {
  double fps = 0.0;
  for (int i = 0; i < g_windows.capacity; i++) {
    struct bucket* bucket = g_windows.buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      struct settings* settings = border ? border_get_settings(border) : NULL;
      if (border && border->animating) fps = fmax(fps, ANIMATION_FPS);
      if (settings
          && animation_border_shimmer_enabled(border, settings)
          && isfinite(settings->shimmer_fps)
          && settings->shimmer_fps > 0.0f) {
        fps = fmax(fps, fmin(settings->shimmer_fps, SHIMMER_MAX_FPS));
      }
      bucket = bucket->next;
    }
  }
  return fps;
}

float animation_ease(enum animation_easing easing, float progress) {
  if (progress <= 0.0f) return 0.0f;
  if (progress >= 1.0f) return 1.0f;

  switch (easing) {
  case ANIMATION_EASING_EASE_IN_EXPO:
    return powf(2.0f, 10.0f * progress - 10.0f);
  case ANIMATION_EASING_EASE_OUT_EXPO:
    return 1.0f - powf(2.0f, -10.0f * progress);
  case ANIMATION_EASING_EASE_IN_OUT_EXPO:
    return progress < 0.5f
           ? powf(2.0f, 20.0f * progress - 10.0f) / 2.0f
           : (2.0f - powf(2.0f, -20.0f * progress + 10.0f)) / 2.0f;
  case ANIMATION_EASING_LINEAR:
  default:
    return progress;
  }
}

void animation_init(struct animation* animation) {
  memset(animation, 0, sizeof(struct animation));
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
void animation_start(struct animation* animation, void* proc, void* context) {
  assert(animation->link == NULL);
  assert(animation->context == NULL);
  CVDisplayLinkCreateWithActiveCGDisplays(&animation->link);
  CVTime refresh_period
            = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(animation->link);
  animation->frame_time = 1e6 * (double)refresh_period.timeValue
                        / (double)refresh_period.timeScale;

  animation->context = context;
  CVDisplayLinkSetOutputCallback(animation->link, proc, animation);
  CVDisplayLinkStart(animation->link);
}

void animation_stop(struct animation* animation) {
  if (animation->link) {
    CVDisplayLinkStop(animation->link);
    CVDisplayLinkRelease(animation->link);
    animation->link = NULL;
  }
  if (animation->context) free(animation->context);
  animation->context = NULL;
}
#pragma clang diagnostic pop

static void animation_tick_callback(CFRunLoopTimerRef timer, void* info) {
  (void)timer; (void)info;
  bool any_animating = false;
  CFTimeInterval now = CACurrentMediaTime();
  for (int i = 0; i < g_windows.capacity; i++) {
    struct bucket* bucket = g_windows.buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      struct settings* settings = border ? border_get_settings(border) : NULL;
      if (settings
          && animation_border_shimmer_enabled(border, settings)
          && isfinite(settings->shimmer_fps)
          && settings->shimmer_fps > 0.0f) {
        float interval = 1.0f / settings->shimmer_fps;
        if (now - border->shimmer_last_draw >= interval) {
          border->shimmer_last_draw = now;
          border->needs_redraw = true;
          border_update(border, false);
        }
        any_animating = true;
      }
      if (border->animating) {
        float progress = border->anim_duration > 0.0f
                         ? (float)((now - border->anim_start)
                                   / border->anim_duration)
                         : 1.0f;
        if (progress >= 1.0f) progress = 1.0f;
        border->anim_alpha = border->focused ? progress : 1.0f - progress;
        if (progress >= 1.0f) {
          border->animating = false;
          border->anim_origin_override = false;
          border->anim_mode = 0;
          border->anim_duration = 0.0f;
          border->needs_redraw = true;
          border_update(border, false);
        } else {
          border_update_animating(border, progress);
          any_animating = true;
        }
      }
      bucket = bucket->next;
    }
  }
  if (!any_animating) {
    animation_stop_ticker();
  } else if (g_anim_timer) {
    double fps = animation_required_fps();
    double interval = fps > 0.0 ? 1.0 / fps : 0.0;
    if (interval > 0.0
        && fabs(CFRunLoopTimerGetInterval(g_anim_timer) - interval) >= 1e-6) {
      animation_stop_ticker();
      animation_start_ticker();
    }
  }
}

void animation_start_ticker(void) {
  double fps = animation_required_fps();
  if (fps <= 0.0) return;

  double interval = 1.0 / fps;
  if (g_anim_timer
      && fabs(CFRunLoopTimerGetInterval(g_anim_timer) - interval) < 1e-6) {
    return;
  }
  animation_stop_ticker();
  g_anim_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                      CFAbsoluteTimeGetCurrent() + interval,
                                      interval,
                                      0, 0,
                                      animation_tick_callback, NULL);
  if (!g_anim_timer) return;
  CFRunLoopAddTimer(CFRunLoopGetMain(), g_anim_timer, kCFRunLoopCommonModes);
}

void animation_stop_ticker(void) {
  if (g_anim_timer) {
    CFRunLoopTimerInvalidate(g_anim_timer);
    CFRelease(g_anim_timer);
    g_anim_timer = NULL;
  }
}

bool animation_is_running(void) {
  return g_anim_timer != NULL;
}
