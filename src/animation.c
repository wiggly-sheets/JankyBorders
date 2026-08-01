#include "animation.h"
#include <CoreFoundation/CoreFoundation.h>
#include <QuartzCore/QuartzCore.h>
#include <math.h>
#include "border.h"
#include "hashtable.h"

extern struct table g_windows;

float g_animation_duration = 0.25f;

static CFRunLoopTimerRef g_anim_timer = NULL;

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
      if (border->animating) {
        float progress = g_animation_duration > 0.0f
                         ? (float)((now - border->anim_start)
                                   / g_animation_duration)
                         : 1.0f;
        if (progress >= 1.0f) progress = 1.0f;
        border->anim_alpha = progress;
        border_update_animating(border, progress);
        if (progress >= 1.0f) {
          border->animating = false;
          border->anim_origin_override = false;
          border->anim_mode = 0;
          border->needs_redraw = true;
          border_update(border, false);
        } else {
          any_animating = true;
        }
      }
      bucket = bucket->next;
    }
  }
  if (!any_animating) {
    animation_stop_ticker();
  }
}

void animation_start_ticker(void) {
  if (g_anim_timer) return;
  g_anim_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                      CFAbsoluteTimeGetCurrent() + 1.0 / 60.0,
                                      1.0 / 60.0,
                                      0, 0,
                                      animation_tick_callback, NULL);
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
