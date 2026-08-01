#pragma once
#include <pthread.h>
#include "misc/helpers.h"
#include "misc/window.h"
#include "misc/drawing.h"
#include "animation.h"
#include "hashtable.h"

#define BORDER_ORDER_ABOVE 1
#define BORDER_ORDER_BELOW -1
#define BORDER_STYLE_ROUND  'r'
#define BORDER_STYLE_ROUND_UNIFORM 'u'
#define BORDER_STYLE_SQUARE 's'
#define BORDER_PADDING 8.0
#define BORDER_TSMN 3.27f
#define BACKGROUND_EVICTION_DELAY_SECONDS 10

#define ANIM_FADE   (1 << 0)
#define ANIM_RAMP   (1 << 1)
#define ANIM_SLIDE  (1 << 2)
#define ANIM_PULSE  (1 << 3)

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
#define BORDER_TSMW 52.f
#else
#define BORDER_TSMW 8.f
#endif

struct color_style {
  enum { COLOR_STYLE_GRADIENT, COLOR_STYLE_SOLID } stype;
  bool glow;
  union {
    uint32_t color;
    struct gradient gradient;
  };
};

struct settings {
  bool enabled;
  uint32_t apply_to;

  struct color_style active_window;
  struct color_style inactive_window;
  struct color_style corner_mask;
  struct color_style background;
  struct color_style active_background;
  struct color_style inactive_background;

  float border_width;
  float blur_radius;
  float active_blur_radius;
  float inactive_blur_radius;
  char border_style;
  bool hidpi;
  bool active_background_override;
  bool inactive_background_override;
  bool active_blur_override;
  bool inactive_blur_override;
  int border_order;
  bool ax_focus;
  int animation;
  float animation_duration;
  enum animation_easing animation_easing;

  bool blacklist_enabled;
  struct table blacklist;

  bool whitelist_enabled;
  struct table whitelist;
};

enum border_background_host {
  BORDER_BACKGROUND_NONE,
  BORDER_BACKGROUND_BORDER,
  BORDER_BACKGROUND_COMPANION,
};

static inline uint32_t border_background_color(const struct settings* settings,
                                               bool focused) {
  if (focused && settings->active_background_override) {
    return settings->active_background.color;
  }
  if (!focused && settings->inactive_background_override) {
    return settings->inactive_background.color;
  }
  return settings->background.color;
}

static inline float border_background_blur_radius(const struct settings* settings,
                                                  bool focused) {
  if (focused && settings->active_blur_override) {
    return settings->active_blur_radius;
  }
  if (!focused && settings->inactive_blur_override) {
    return settings->inactive_blur_radius;
  }
  return settings->blur_radius;
}

static inline bool border_background_visible(const struct settings* settings,
                                             bool focused) {
  return (border_background_color(settings, focused) & 0xff000000) != 0;
}

static inline enum border_background_host border_background_host(
    const struct settings* settings,
    bool focused) {
  if (!border_background_visible(settings, focused)
      && border_background_blur_radius(settings, focused) <= 0.0f) {
    return BORDER_BACKGROUND_NONE;
  }
  return settings->border_order == BORDER_ORDER_ABOVE
         ? BORDER_BACKGROUND_COMPANION
         : BORDER_BACKGROUND_BORDER;
}

static inline bool border_should_update_background_placement(
    bool has_background_window,
    bool animating,
    bool placement_needed) {
  return has_background_window && (!animating || placement_needed);
}

struct event_buffer {
  bool disable_coalescing;
  volatile bool is_coalescing;
  int64_t last_coalesce_attempt;
};

struct border {
  pthread_mutex_t mutex;
  int cid;

  bool focused;
  bool needs_redraw;
  bool too_small;
  bool sticky;

  uint64_t sid;
  uint32_t wid;
  uint32_t target_wid;

  float radius;
  float inner_radius;

  CGPoint origin;
  CGRect frame;
  CGRect target_bounds;
  CGRect drawing_bounds;
  CGContextRef context;
  uint32_t border_blur_radius;

  uint32_t background_wid;
  CGContextRef background_context;
  CGRect background_frame;
  uint32_t background_color;
  uint32_t background_blur_radius;
  uint64_t background_eviction_token;

  struct animation animation;
  struct event_buffer event_buffer;
  bool animating;
  int anim_mode;
  CFTimeInterval anim_start;
  float anim_duration;
  CGPoint anim_start_origin;
  CGPoint anim_end_origin;
  CGPoint anim_current_origin;
  bool anim_origin_override;
  float anim_alpha;
  float anim_stroke_width;

  bool is_proxy;
  bool is_destroyed;
  struct border* proxy;
  volatile uint32_t external_proxy_wid;

  struct settings setting_override;
};

struct border* border_create();
void border_destroy(struct border* border);

void border_move(struct border* border);
void border_update(struct border* border, bool try_async);
void border_update_animating(struct border* border, float progress);
void border_hide(struct border* border);
void border_unhide(struct border* border);

struct settings* border_get_settings(struct border* border);
