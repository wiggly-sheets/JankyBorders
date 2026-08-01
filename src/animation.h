#pragma once
#include <CoreVideo/CoreVideo.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <pthread.h>

#define ANIMATION_PULSE_MAX_EXPANSION 6.0f

enum animation_easing {
  ANIMATION_EASING_LINEAR,
  ANIMATION_EASING_EASE_IN_EXPO,
  ANIMATION_EASING_EASE_OUT_EXPO,
  ANIMATION_EASING_EASE_IN_OUT_EXPO,
};

struct animation {
  void* context;
  double frame_time;
  CVDisplayLinkRef link;
};

static inline float animation_lerp(float start, float end, float progress) {
  return start + progress * (end - start);
}

static inline float animation_pulse_scale(float progress) {
  return progress < 0.5f
         ? 1.0f + 2.0f * progress
         : 3.0f - 2.0f * progress;
}

static inline float animation_pulse_width(float border_width, float progress) {
  float normalized_width = fmaxf(border_width, 1.0f);
  float expansion = ANIMATION_PULSE_MAX_EXPANSION / sqrtf(normalized_width);
  return border_width + expansion * (animation_pulse_scale(progress) - 1.0f);
}

float animation_ease(enum animation_easing easing, float progress);

void animation_init(struct animation* animation);
void animation_start(struct animation* animation, void* proc, void* context);
void animation_stop(struct animation* animation);
void animation_start_ticker(void);
void animation_stop_ticker(void);
bool animation_is_running(void);
