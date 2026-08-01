#pragma once
#include <CoreVideo/CoreVideo.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>

struct animation {
  void* context;
  double frame_time;
  CVDisplayLinkRef link;
};

void animation_init(struct animation* animation);
void animation_start(struct animation* animation, void* proc, void* context);
void animation_stop(struct animation* animation);
void animation_start_ticker(void);
void animation_stop_ticker(void);
bool animation_is_running(void);
extern float g_animation_duration;
