#include "animation.h"
#include <CoreFoundation/CoreFoundation.h>
#include <QuartzCore/QuartzCore.h>
#include "border.h"
#include "hashtable.h"

extern struct settings g_settings;
extern struct table g_windows;

float g_animation_duration = 0.25f;

static CFRunLoopTimerRef g_anim_timer = NULL;

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
  for (int i = 0; i < g_windows.capacity; i++) {
    struct bucket* bucket = g_windows.buckets[i];
    while (bucket) {
      struct border* border = bucket->value;
      if (border->animating) {
        any_animating = true;
        CFTimeInterval now = CACurrentMediaTime();
        float progress = (float)((now - border->anim_start) / g_animation_duration);
        if (progress >= 1.0f) progress = 1.0f;
        border->anim_alpha = progress;
        border_update_animating(border, progress);
        if (progress >= 1.0f) {
          border->animating = false;
          border->needs_redraw = true;
          border_update(border, false);
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
  if (g_anim_timer) {
    CFRunLoopTimerInvalidate(g_anim_timer);
    CFRelease(g_anim_timer);
  }
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
