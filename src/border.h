#pragma once
#include <pthread.h>
#include "misc/helpers.h"
#include "misc/window.h"
#include "misc/drawing.h"
#include "animation.h"
#include "hashtable.h"

#define BORDER_ORDER_ABOVE 1
#define BORDER_ORDER_BELOW -1
#define BORDER_POSITION_OUTSIDE 0
#define BORDER_POSITION_INSIDE 1
#define BORDER_STYLE_ROUND  'r'
#define BORDER_STYLE_ROUND_UNIFORM 'u'
#define BORDER_STYLE_SQUARE 's'
#define BORDER_STYLE_NONE 'n'
#define BORDER_PADDING 8.0
#define BORDER_TSMN 3.27f
#define BORDER_DEFAULT_RADIUS 9
#define BACKGROUND_EVICTION_DELAY_SECONDS 10
#define SHIMMER_MAX_COLORS 16

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
  enum { COLOR_STYLE_GRADIENT, COLOR_STYLE_SOLID, COLOR_STYLE_MULTI } stype;
  bool glow;
  union {
    uint32_t color;
    struct gradient gradient;
    struct {
      uint32_t left;
      uint32_t top;
      uint32_t right;
      uint32_t bottom;
    } multi;
  };
};

struct border_appearance {
  uint32_t layer_count;
  struct color_style layers[2];
};

enum border_background_mode {
  BORDER_BACKGROUND_AUTO,
  BORDER_BACKGROUND_FORCE_BORDER,
  BORDER_BACKGROUND_FORCE_COMPANION,
};

struct settings {
  bool enabled;
  uint32_t apply_to;

  struct border_appearance active_window;
  struct border_appearance inactive_window;
  struct color_style corner_mask;
  struct color_style background;
  struct color_style active_background;
  struct color_style inactive_background;

  float border_width;
  float inner_border_width;
  float double_border_gap;
  float blur_radius;
  float active_blur_radius;
  float inactive_blur_radius;
  char border_style;
  bool hidpi;
  bool active_background_override;
  bool inactive_background_override;
  bool active_blur_override;
  bool inactive_blur_override;
  bool inactive_foreground;
  enum border_background_mode background_mode;
  int border_order;
  int border_position;
  bool ax_focus;
  bool active_only;
  int animation;
  int inactive_animation;
  float animation_duration;
  enum animation_easing animation_easing;

  uint32_t shimmer_colors[SHIMMER_MAX_COLORS];
  uint32_t shimmer_color_count;
  uint32_t inactive_shimmer_colors[SHIMMER_MAX_COLORS];
  uint32_t inactive_shimmer_color_count;
  float shimmer_duration;
  float shimmer_fps;
  uint32_t stack_color;
  uint32_t floating_color;
  uint32_t bsp_color;
  bool stack_color_override;
  bool floating_color_override;
  bool bsp_color_override;
  int window_state;

  bool blacklist_enabled;
  struct table blacklist;

  bool whitelist_enabled;
  struct table whitelist;
};

enum border_window_state {
  BORDER_WINDOW_STATE_NONE,
  BORDER_WINDOW_STATE_STACK,
  BORDER_WINDOW_STATE_FLOATING,
  BORDER_WINDOW_STATE_BSP,
};

static inline bool settings_shimmer_enabled(const struct settings* settings) {
  return settings->shimmer_color_count >= 2
         || settings->inactive_shimmer_color_count >= 2;
}

static inline float border_appearance_extent(const struct settings* settings,
                                             bool focused) {
  const struct border_appearance* appearance = focused
                                               ? &settings->active_window
                                               : &settings->inactive_window;
  if (appearance->layer_count < 2) return settings->border_width;
  return settings->border_width
         + settings->inner_border_width
         + settings->double_border_gap;
}

static inline float border_max_extent(const struct settings* settings) {
  return fmaxf(border_appearance_extent(settings, true),
               border_appearance_extent(settings, false));
}

static inline int border_effective_order(const struct settings* settings) {
  return settings->border_position == BORDER_POSITION_INSIDE
         ? BORDER_ORDER_ABOVE
         : settings->border_order;
}

static inline float border_layer_corner_radius(float base_radius,
                                               float center_offset) {
  return base_radius <= 0.0f ? 0.0f : base_radius + center_offset;
}

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
  if (!focused && settings->inactive_foreground) {
    return BORDER_BACKGROUND_COMPANION;
  }
  if (settings->background_mode == BORDER_BACKGROUND_FORCE_BORDER) {
    return BORDER_BACKGROUND_BORDER;
  }
  if (settings->background_mode == BORDER_BACKGROUND_FORCE_COMPANION) {
    return BORDER_BACKGROUND_COMPANION;
  }
  return border_effective_order(settings) == BORDER_ORDER_ABOVE
         ? BORDER_BACKGROUND_COMPANION
         : BORDER_BACKGROUND_BORDER;
}

static inline bool border_should_update_background_placement(
    bool has_background_window,
    bool animating,
    bool placement_needed) {
  // Focus/reorder events can change the target stacking relation without
  // changing its frame. Re-apply placement so above/below blur stays attached.
  (void)animating;
  (void)placement_needed;
  return has_background_window;
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
  enum border_window_state window_state;
  uint64_t update_generation;

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
  CFTimeInterval shimmer_last_draw;

  bool is_proxy;
  bool is_destroyed;
  struct border* proxy;
  volatile uint32_t external_proxy_wid;

  struct settings setting_override;
};

static inline void border_set_detected_radius(struct border* border,
                                              int32_t detected_radius) {
  if (detected_radius < 0) detected_radius = BORDER_DEFAULT_RADIUS;

  bool nearly_square = detected_radius <= 1;
  border->radius = nearly_square ? 0.0f : detected_radius;
  border->inner_radius = nearly_square
                         ? BORDER_TSMN
                         : detected_radius + 1.0f;
}

struct border* border_create();
void border_destroy(struct border* border);

void border_move(struct border* border);
void border_update(struct border* border, bool try_async);
void border_space_change_begin(void);
void border_update_animating(struct border* border, float progress);
void border_hide(struct border* border);
void border_unhide(struct border* border);

struct settings* border_get_settings(struct border* border);
