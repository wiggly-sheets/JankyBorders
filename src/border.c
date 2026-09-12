#include "border.h"
#include "hashtable.h"
#include "layer.h"
#include "misc/extern.h"
#include "windows.h"
#include <QuartzCore/QuartzCore.h>
#include <pthread.h>
#include <time.h>

extern struct settings g_settings;
extern struct table g_windows;

static uint64_t g_background_eviction_token = 0;

static bool border_bounds_are_valid(CGRect bounds) {
  return isfinite(bounds.origin.x)
         && isfinite(bounds.origin.y)
         && isfinite(bounds.size.width)
         && isfinite(bounds.size.height)
         && bounds.size.width > 0.0
         && bounds.size.height > 0.0;
}

static uint32_t border_shimmer_color(const uint32_t* colors,
                                     uint32_t count,
                                     float duration) {
  // CFAbsoluteTime is roughly 800 million seconds today. Converting it to a
  // float loses sub-minute precision and makes the palette appear frozen.
  // Use the monotonic display clock as a double, as the original shimmer loop
  // effectively did, and make duration the time between adjacent colors.
  double position = CACurrentMediaTime() / (double)duration;
  uint32_t index = (uint32_t)floor(position) % count;
  uint32_t next = (index + 1) % count;
  float progress = (float)(position - floor(position));
  uint32_t from = colors[index];
  uint32_t to = colors[next];
  uint32_t result = 0;
  for (int shift = 0; shift <= 24; shift += 8) {
    uint32_t start = (from >> shift) & 0xff;
    uint32_t end = (to >> shift) & 0xff;
    result |= (uint32_t)lroundf(animation_lerp(start, end, progress)) << shift;
  }
  return result;
}

static void border_recreate_context(struct border* border) {
  if (border->context) CGContextRelease(border->context);
  border->context = border->wid
                    ? SLWindowContextCreate(border->cid, border->wid, NULL)
                    : NULL;
  if (border->context) {
    CGContextSetInterpolationQuality(border->context, kCGInterpolationNone);
  }
}

static void border_recreate_background_context(struct border* border) {
  if (border->background_context) CGContextRelease(border->background_context);
  border->background_context = border->background_wid
                               ? SLWindowContextCreate(border->cid,
                                                       border->background_wid,
                                                       NULL)
                               : NULL;
  if (border->background_context) {
    CGContextSetInterpolationQuality(border->background_context,
                                     kCGInterpolationNone);
  }
}

struct settings* border_get_settings(struct border* border) {
  assert(pthread_main_np() != 0);
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

static void border_destroy_window(struct border* border) {
  if (border->layer) layer_border_destroy(border->layer);
  if (border->context) CGContextRelease(border->context);
  if (border->wid) SLSReleaseWindow(border->cid, border->wid);
  drawing_nine_slice_release(&border->nine_slice);
  border->layer = NULL;
  border->use_layer = false;
  border->wid = 0;
  border->context = NULL;
  border->border_blur_radius = 0;
}

static void border_destroy_background_window(struct border* border) {
  border->background_eviction_token = 0;
  if (border->background_wid) {
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      SLSTransactionOrderWindow(transaction,
                                border->background_wid,
                                0,
                                border->target_wid);
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }
  }
  if (border->background_context) CGContextRelease(border->background_context);
  if (border->background_wid) SLSReleaseWindow(border->cid,
                                               border->background_wid);
  border->background_wid = 0;
  border->background_context = NULL;
  border->background_frame = CGRectNull;
  border->background_color = 0;
  border->background_blur_radius = 0;
}

static void border_cancel_background_eviction(struct border* border) {
  border->background_eviction_token = 0;
}

static void border_schedule_background_eviction(struct border* border) {
  if (!border->background_wid || border->background_eviction_token) return;

  uint32_t target_wid = border->target_wid;
  uint64_t token = ++g_background_eviction_token;
  border->background_eviction_token = token;

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               BACKGROUND_EVICTION_DELAY_SECONDS * NSEC_PER_SEC),
                 dispatch_get_main_queue(), ^{
    uint32_t lookup_wid = target_wid;
    struct border* current = table_find(&g_windows, &lookup_wid);
    if (!current) return;

    pthread_mutex_lock(&current->mutex);
    if (current->background_eviction_token == token) {
      bool shown = false;
      SLSWindowIsOrderedIn(current->cid, current->target_wid, &shown);
      bool visible = current->sticky
                     || is_space_visible(current->cid, current->sid);
      if (!shown || !visible) {
        border_destroy_background_window(current);
      } else {
        border_cancel_background_eviction(current);
      }
    }
    pthread_mutex_unlock(&current->mutex);
  });
}

static uint32_t border_background_relative_wid(struct border* border,
                                               struct settings* settings) {
  // above: background < target < border
  // below: background < border < target
  return border_effective_order(settings) == BORDER_ORDER_BELOW
         ? border->wid
         : border->target_wid;
}

static void border_create_background_window(struct border* border,
                                            CGRect frame,
                                            bool hidpi) {
  border->background_wid = window_create(border->cid, frame, hidpi, false);
  border_recreate_background_context(border);
  border->background_frame = frame;
  border->background_color = UINT32_MAX;
  border->background_blur_radius = UINT32_MAX;
  window_send_to_space(border->cid, border->background_wid, border->sid);
}

static void border_set_blur_radius(struct border* border,
                                   uint32_t wid,
                                   uint32_t* current_radius,
                                   float radius) {
  uint32_t blur_radius = (uint32_t)radius;
  if (wid && blur_radius != *current_radius) {
    SLSSetWindowBackgroundBlurRadius(border->cid, wid, blur_radius);
    *current_radius = blur_radius;
  }
}

static void border_draw_background(struct border* border,
                                   CGRect frame,
                                   struct settings* settings) {
  if (!border->background_context) return;
  CGRect bounds = { .origin = CGPointZero, .size = frame.size };
  CGContextSaveGState(border->background_context);
  CGContextClearRect(border->background_context, bounds);

  if (border_background_visible(settings, border->focused)) {
    CGPathRef background_path = CGPathCreateWithRoundedRect(
        CGRectInset(bounds, 1.0, 1.0),
        border->inner_radius,
        border->inner_radius,
        NULL);
    drawing_draw_filled_path(border->background_context,
                             background_path,
                             border_background_color(settings,
                                                     border->focused));
    CFRelease(background_path);
  }

  CGContextFlush(border->background_context);
  CGContextRestoreGState(border->background_context);
  SLSFlushWindowContentRegion(border->cid, border->background_wid, NULL);
  SLSWindowThaw(border->cid, border->background_wid);
}

static bool border_update_background(struct border* border,
                                     CGRect frame,
                                     struct settings* settings) {
  border_cancel_background_eviction(border);
  bool created = false;
  if (!border->background_wid) {
    border_create_background_window(border, frame, settings->hidpi);
    created = true;
  }
  if (!border->background_context) {
    border_recreate_background_context(border);
  }

  bool frame_changed = !CGRectEqualToRect(frame, border->background_frame);
  if (frame_changed) {
    CFTypeRef frame_region;
    CGSNewRegionWithRect(&frame, &frame_region);
    SLSWindowFreezeWithOptions(border->cid,
                               border->background_wid,
                               NULL);
    SLSSetWindowShape(border->cid,
                      border->background_wid,
                      frame.origin.x,
                      frame.origin.y,
                      frame_region);
    CFRelease(frame_region);
    border_recreate_background_context(border);
    border->background_frame = frame;
  }

  uint32_t background_color = border_background_visible(settings,
                                                        border->focused)
                              ? border_background_color(settings,
                                                        border->focused)
                              : 0;
  if (border->background_context
      && (frame_changed || background_color != border->background_color)) {
    border_draw_background(border, frame, settings);
    border->background_color = background_color;
  }

  border_set_blur_radius(border,
                         border->background_wid,
                         &border->background_blur_radius,
                         border_background_blur_radius(settings,
                                                       border->focused));
  border->tags_applied = false;
  border->applied_sticky = false;
  return created || frame_changed;
}

static bool border_check_too_small(struct border* border, CGRect window_frame) {
  CGRect smallest_rect = CGRectInset(window_frame, 1.0, 1.0);
  if (smallest_rect.size.width < 2.f * border->inner_radius
      || smallest_rect.size.height < 2.f * border->inner_radius) {
    return true;
  }
  return false;
}

static bool border_calculate_bounds(struct border* border,
                                    CGRect* frame,
                                    struct settings* settings) {
  CGRect window_frame = CGRectNull;
  if (border->is_proxy) {
    window_frame = border->target_bounds;
  } else if (SLSGetWindowBounds(border->cid,
                                border->target_wid,
                                &window_frame) != kCGErrorSuccess) {
    return false;
  }
  if (!border_bounds_are_valid(window_frame)) return false;

  border->target_bounds = window_frame;
  border->too_small = border_check_too_small(border, window_frame);
  if (border->too_small) {
    border_hide(border);
    return false;
  }

  float border_offset = settings->border_position == BORDER_POSITION_INSIDE
                        ? 0.0f
                        : -border_max_extent(settings) - BORDER_PADDING;
  *frame = CGRectInset(window_frame, border_offset, border_offset);

  border->origin = frame->origin;
  frame->origin = CGPointZero;


  window_frame.origin = (CGPoint){ -border_offset, -border_offset };
  border->drawing_bounds = window_frame;

  return true;
}

static void border_add_gradient_glow_path(CGContextRef context,
                                          CGRect path_rect,
                                          float inset,
                                          float corner_radius,
                                          bool square) {
  if (square) drawing_add_rect_with_inset(context, path_rect, inset);
  else drawing_add_rounded_rect(context, path_rect, corner_radius);
}

static void border_draw_gradient_glow(CGContextRef context,
                                      const struct gradient* gradient,
                                      CGRect path_rect,
                                      float inset,
                                      float corner_radius,
                                      float blur_radius,
                                      bool square) {
  float a, r, g, b;
  colors_mix(gradient->color1, gradient->color2, &a, &r, &g, &b);
  CGColorRef glow_color = CGColorCreateGenericRGB(r, g, b, a);

  CGContextSaveGState(context);
  CGContextSetShadowWithColor(context, CGSizeZero, blur_radius, glow_color);
  CGColorRelease(glow_color);
  CGContextSetRGBFillColor(context, 1.0f, 1.0f, 1.0f, 1.0f);
  CGContextSetRGBStrokeColor(context, 1.0f, 1.0f, 1.0f, 1.0f);
  border_add_gradient_glow_path(context,
                                path_rect,
                                inset,
                                corner_radius,
                                square       );
  if (square) CGContextFillPath(context);
  else CGContextStrokePath(context);

  CGContextSetShadowWithColor(context, CGSizeZero, 0, NULL);
  CGContextSetBlendMode(context, kCGBlendModeDestinationOut);
  border_add_gradient_glow_path(context,
                                path_rect,
                                inset,
                                corner_radius,
                                square       );
  if (square) CGContextFillPath(context);
  else CGContextStrokePath(context);
  CGContextRestoreGState(context);
}

static float border_animation_glow_blur(struct border* border, float base) {
  if (!border->animating || !(border->anim_mode & ANIM_RAMP)) return base;
  return border->anim_alpha * base;
}

static void border_double_layer_centers(bool inside,
                                        float outer_width,
                                        float inner_width,
                                        float gap,
                                        float* outer_center,
                                        float* inner_center) {
  if (inside) {
    *outer_center = -outer_width / 2.0f;
    *inner_center = -(outer_width + gap + inner_width / 2.0f);
  } else {
    *outer_center = inner_width + gap + outer_width / 2.0f;
    *inner_center = inner_width / 2.0f;
  }
}

static void border_draw_double_layer(struct border* border,
                                     const struct color_style* style,
                                     CGRect frame,
                                     CGRect path_rect,
                                     float center_offset,
                                     float width,
                                     float corner_radius,
                                     bool square) {
  CGContextRef context = border->context;
  CGRect layer_rect = CGRectInset(path_rect, -center_offset, -center_offset);
  float layer_corner_radius = fmaxf(border_layer_corner_radius(corner_radius,
                                                               center_offset),
                                    0.0f);
  CGContextSaveGState(context);
  CGContextSetLineWidth(context, width);

  if (style->stype == COLOR_STYLE_SOLID) {
    drawing_set_stroke_and_fill(context, style->color, style->glow);
    if (style->glow && border->animating && (border->anim_mode & ANIM_RAMP)) {
      float a, r, g, b;
      colors_from_hex(style->color, &a, &r, &g, &b);
      CGColorRef glow_color = CGColorCreateGenericRGB(r, g, b, 1.0f);
      CGContextSetShadowWithColor(context,
                                  CGSizeZero,
                                  border_animation_glow_blur(border, 10.0f),
                                  glow_color);
      CGColorRelease(glow_color);
    }
    if (square) drawing_add_rect_with_inset(context, layer_rect, 0.0f);
    else drawing_add_rounded_rect(context,
                                  layer_rect,
                                  layer_corner_radius);
    CGContextStrokePath(context);
  } else {
    CGPoint gradient_dir[2];
    CGAffineTransform transform = CGAffineTransformMakeScale(frame.size.width,
                                                             frame.size.height);
    CGGradientRef gradient = drawing_create_gradient(&style->gradient,
                                                     transform,
                                                     gradient_dir);
    if (!gradient) {
      drawing_set_stroke(context, style->gradient.color1);
      if (square) drawing_add_rect_with_inset(context, layer_rect, 0.0f);
      else drawing_add_rounded_rect(context,
                                    layer_rect,
                                    layer_corner_radius);
      CGContextStrokePath(context);
      CGContextRestoreGState(context);
      return;
    }

    if (style->glow) {
      float blur_radius = border_animation_glow_blur(border, 10.0f);
      border_draw_gradient_glow(context,
                                &style->gradient,
                                layer_rect,
                                0.0f,
                                layer_corner_radius,
                                blur_radius,
                                square);
    }

    if (square) drawing_add_rect_with_inset(context, layer_rect, 0.0f);
    else drawing_add_rounded_rect(context,
                                  layer_rect,
                                  layer_corner_radius);
    CGContextReplacePathWithStrokedPath(context);
    CGContextClip(context);
    CGContextDrawLinearGradient(context,
                                gradient,
                                gradient_dir[0],
                                gradient_dir[1],
                                0);
    CGGradientRelease(gradient);
  }
  CGContextRestoreGState(context);
}

static void border_draw_multi_color(struct border* border,
                                    CGRect path_rect,
                                    float inset,
                                    float corner_radius,
                                    bool square,
                                    bool inside,
                                    const struct color_style* style) {
  CGContextRef context = border->context;
  CGFloat band = fmaxf(-inset * 4.0f, 1.0f);
  struct {
    CGRect rect;
    uint32_t color;
  } edges[] = {
    { CGRectMake(CGRectGetMinX(path_rect) - band,
                 CGRectGetMinY(path_rect) - band,
                 CGRectGetWidth(path_rect) + band * 2.0f,
                 band * 2.0f), style->multi.top },
    { CGRectMake(CGRectGetMinX(path_rect) - band,
                 CGRectGetMaxY(path_rect) - band,
                 CGRectGetWidth(path_rect) + band * 2.0f,
                 band * 2.0f), style->multi.bottom },
    { CGRectMake(CGRectGetMinX(path_rect) - band,
                 CGRectGetMinY(path_rect) - band,
                 band * 2.0f,
                 CGRectGetHeight(path_rect) + band * 2.0f), style->multi.left },
    { CGRectMake(CGRectGetMaxX(path_rect) - band,
                 CGRectGetMinY(path_rect) - band,
                 band * 2.0f,
                 CGRectGetHeight(path_rect) + band * 2.0f), style->multi.right },
  };

  for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
    CGContextSaveGState(context);
    CGContextClipToRect(context, edges[i].rect);
    drawing_set_stroke_and_fill(context, edges[i].color, false);
    if (square) {
      if (inside) {
        drawing_add_rect_with_inset(context, path_rect, 0.0f);
        CGContextStrokePath(context);
      } else {
        drawing_draw_square_with_inset(context, path_rect, inset);
      }
    } else {
      drawing_draw_rounded_rect_with_inset(context,
                                           path_rect,
                                           corner_radius,
                                           false);
    }
    CGContextRestoreGState(context);
  }
}

static void border_clear(struct border* border, CGRect frame) {
  CGContextClearRect(border->context, frame);
}

static void border_draw_slow(struct border* border, CGRect frame, struct settings* settings) {
  CGContextSaveGState(border->context);
  border->needs_redraw = false;
  struct border_appearance appearance = border->focused
                                        ? settings->active_window
                                        : settings->inactive_window;
  const uint32_t* shimmer_colors = border->focused
                                   ? settings->shimmer_colors
                                   : settings->inactive_shimmer_colors;
  uint32_t shimmer_count = border->focused
                           ? settings->shimmer_color_count
                           : settings->inactive_shimmer_color_count;
  if (shimmer_count >= 2) {
    appearance.layer_count = 1;
    appearance.layers[0] = (struct color_style) {
      .stype = COLOR_STYLE_SOLID,
      .color = border_shimmer_color(shimmer_colors,
                                    shimmer_count,
                                    settings->shimmer_duration),
    };
  }
  uint32_t state_color = 0;
  bool state_color_override = false;
  if (border->window_state == BORDER_WINDOW_STATE_STACK
      && settings->stack_color_override) {
    state_color = settings->stack_color;
    state_color_override = true;
  }
  else if (border->window_state == BORDER_WINDOW_STATE_FLOATING
           && settings->floating_color_override) {
    state_color = settings->floating_color;
    state_color_override = true;
  }
  else if (border->window_state == BORDER_WINDOW_STATE_BSP
           && settings->bsp_color_override) {
    state_color = settings->bsp_color;
    state_color_override = true;
  }
  if (state_color_override) {
    appearance.layer_count = 1;
    appearance.layers[0] = (struct color_style) {
      .stype = COLOR_STYLE_SOLID, .color = state_color,
    };
  }
  bool is_double = appearance.layer_count == 2;
  struct color_style color_style = appearance.layers[0];
  float base_extent = border_appearance_extent(settings, border->focused);
  float effective_extent = border->animating
                           && (border->anim_mode & ANIM_PULSE)
                           ? border->anim_stroke_width
                           : base_extent;
  float effective_border_width = effective_extent;
  if (border->animating && (border->anim_mode & ANIM_FADE)) {
    CGContextSetAlpha(border->context, border->anim_alpha);
  }

  CGGradientRef gradient = NULL;
  CGPoint gradient_dir[2];
  if (!is_double && color_style.stype == COLOR_STYLE_SOLID) {
    drawing_set_stroke_and_fill(border->context, color_style.color, color_style.glow);
    if (color_style.glow && border->animating && (border->anim_mode & ANIM_RAMP)) {
      float a, r, g, b;
      colors_from_hex(color_style.color, &a, &r, &g, &b);
      CGColorRef glow_color = CGColorCreateGenericRGB(r, g, b, 1.0f);
      CGContextSetShadowWithColor(border->context,
                                  CGSizeZero,
                                  border_animation_glow_blur(border, 10.0f),
                                  glow_color);
      CGColorRelease(glow_color);
    }
  } else if (!is_double && color_style.stype == COLOR_STYLE_GRADIENT) {
    uint32_t fallback_color = color_style.gradient.color1;
    CGAffineTransform trans = CGAffineTransformMakeScale(frame.size.width,
                                                         frame.size.height);
    gradient = drawing_create_gradient(&color_style.gradient,
                                       trans,
                                       gradient_dir          );
    if (!gradient) {
      color_style.stype = COLOR_STYLE_SOLID;
      color_style.glow = false;
      color_style.color = fallback_color;
      drawing_set_stroke_and_fill(border->context,
                                  fallback_color,
                                  false);
    }
  }

  if (!is_double) {
    CGContextSetLineWidth(border->context, effective_border_width);
  }
  border_clear(border, frame);

  bool inside = settings->border_position == BORDER_POSITION_INSIDE;
  CGRect path_rect = border->drawing_bounds;
  CGMutablePathRef inner_clip_path = CGPathCreateMutable();
  bool square_thick_above = settings->border_style == BORDER_STYLE_SQUARE
                            && !inside
                            && border_effective_order(settings) == BORDER_ORDER_ABOVE
                            && border_max_extent(settings) >= BORDER_TSMW;
  if (square_thick_above) {
    // Inset the frame to overlap the rounding of macOS windows to create a
    // truly square border
    path_rect = CGRectInset(border->drawing_bounds,
                            BORDER_TSMN,
                            BORDER_TSMN            );

    CGPathAddRect(inner_clip_path, NULL, path_rect);
  } else {
    CGPathAddRoundedRect(inner_clip_path,
                         NULL,
                         CGRectInset(path_rect, 1.0, 1.0),
                         border->inner_radius,
                         border->inner_radius             );
  }
  if (!inside) {
    drawing_clip_between_rect_and_path(border->context,
                                       frame,
                                       inner_clip_path);
  }

  bool square = settings->border_style == BORDER_STYLE_SQUARE;
  float inset = -effective_border_width / 2.f;
  float corner_radius = settings->border_style == BORDER_STYLE_ROUND_UNIFORM
                        ? 9.0
                        : border->radius;

  if (inside && !is_double) {
    float max_inset = fmaxf(fminf(path_rect.size.width,
                                  path_rect.size.height) / 2.0f - 0.5f,
                            0.0f);
    float path_inset = fminf(effective_border_width / 2.0f, max_inset);
    path_rect = CGRectInset(path_rect, path_inset, path_inset);
    corner_radius = fmaxf(corner_radius - path_inset, 0.0f);
  }

  if (settings->border_style == BORDER_STYLE_ROUND_UNIFORM
      && !inside
      && !is_double) {
    drawing_draw_rounded_rect_with_inset(border->context,
                                         path_rect,
                                         corner_radius,
                                         true            );
  }

  if (is_double) {
    float base_width = settings->border_width + settings->inner_border_width;
    float animated_width = fmaxf(effective_extent
                                 - settings->double_border_gap,
                                 0.0f);
    float width_scale = base_width > 0.0f ? animated_width / base_width : 1.0f;
    float outer_width = settings->border_width * width_scale;
    float inner_width = settings->inner_border_width * width_scale;
    float inner_center;
    float outer_center;
    border_double_layer_centers(inside,
                                outer_width,
                                inner_width,
                                settings->double_border_gap,
                                &outer_center,
                                &inner_center);
    border_draw_double_layer(border,
                             &appearance.layers[0],
                             frame,
                             path_rect,
                             outer_center,
                             outer_width,
                             corner_radius,
                             square);
    border_draw_double_layer(border,
                             &appearance.layers[1],
                             frame,
                             path_rect,
                             inner_center,
                             inner_width,
                             corner_radius,
                             square);
  } else if (color_style.stype == COLOR_STYLE_SOLID) {
    if (square) {
      if (inside) {
        drawing_add_rect_with_inset(border->context, path_rect, 0.0f);
        CGContextStrokePath(border->context);
      } else {
        drawing_draw_square_with_inset(border->context,
                                       path_rect,
                                       inset    );
      }
    } else {
      drawing_draw_rounded_rect_with_inset(border->context,
                                           path_rect,
                                           corner_radius,
                                           false           );
    }
  } else if (color_style.stype == COLOR_STYLE_GRADIENT) {
    if (color_style.glow) {
      float blur_radius = square_thick_above ? BORDER_TSMN : 10.0f;
      blur_radius = border_animation_glow_blur(border, blur_radius);
      border_draw_gradient_glow(border->context,
                                &color_style.gradient,
                                path_rect,
                                inside && square ? 0.0f : inset,
                                corner_radius,
                                blur_radius,
                                square               );
    }

    CGContextSaveGState(border->context);
    if (square) {
      if (inside) {
        drawing_add_rect_with_inset(border->context, path_rect, 0.0f);
        CGContextReplacePathWithStrokedPath(border->context);
        CGContextClip(border->context);
        CGContextDrawLinearGradient(border->context,
                                    gradient,
                                    gradient_dir[0],
                                    gradient_dir[1],
                                    0);
      } else {
        drawing_draw_square_gradient_with_inset(border->context,
                                                gradient,
                                                gradient_dir,
                                                path_rect,
                                                inset       );
      }
    } else {
      drawing_draw_rounded_gradient_with_inset(border->context,
                                               gradient,
                                               gradient_dir,
                                               path_rect,
                                               corner_radius  );
    }
    CGContextRestoreGState(border->context);
  } else if (color_style.stype == COLOR_STYLE_MULTI) {
    border_draw_multi_color(border,
                            path_rect,
                            inset,
                            corner_radius,
                            square,
                            inside,
                            &color_style);
  }
  if (gradient) CGGradientRelease(gradient);

  if (border_background_visible(settings, border->focused)
      && border_background_host(settings,
                                border->focused) == BORDER_BACKGROUND_BORDER) {
    CGContextRestoreGState(border->context);
    CGContextSaveGState(border->context);
    drawing_draw_filled_path(border->context,
                             inner_clip_path,
                             border_background_color(settings,
                                                     border->focused));
  }

  CFRelease(inner_clip_path);
  CGContextFlush(border->context);
  CGContextRestoreGState(border->context);
  SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
  SLSWindowThaw(border->cid, border->wid);
}

static bool border_nine_slice_style(struct border* border, struct settings* settings, struct nine_slice* ns) {
  if (settings->show_background) return false;
  if (settings->border_style != BORDER_STYLE_ROUND
      && settings->border_style != BORDER_STYLE_ROUND_UNIFORM) return false;
  if (settings_shimmer_enabled(settings)) return false;
  if (settings->animation != 0 || settings->inactive_animation != 0) return false;
  if (border->window_state == BORDER_WINDOW_STATE_STACK
      && settings->stack_color_override) return false;
  if (border->window_state == BORDER_WINDOW_STATE_FLOATING
      && settings->floating_color_override) return false;
  if (border->window_state == BORDER_WINDOW_STATE_BSP
      && settings->bsp_color_override) return false;

  struct border_appearance appearance = border->focused
                                        ? settings->active_window
                                        : settings->inactive_window;
  if (appearance.layer_count != 1) return false;

  struct color_style color_style = appearance.layers[0];
  if (color_style.stype != COLOR_STYLE_SOLID) return false;

  ns->color = color_style.color;
  ns->border_width = settings->border_width;
  ns->outer_offset = settings->border_width + BORDER_PADDING;
  ns->uniform = settings->border_style == BORDER_STYLE_ROUND_UNIFORM;
  ns->corner_radius = ns->uniform ? 9.f : border->radius;
  ns->inner_radius = border->inner_radius;
  ns->scale = settings->hidpi ? 2.f : 1.f;
  return true;
}

static bool border_nine_slice_fits(CGRect frame, const struct nine_slice* ns) {
  // The blits are only pixel exact on an integral pixel grid
  float width_px = frame.size.width * ns->scale;
  float height_px = frame.size.height * ns->scale;
  if (width_px != floorf(width_px) || height_px != floorf(height_px))
    return false;

  float extent = drawing_nine_slice_extent(ns);
  return frame.size.width >= 2.f * extent
      && frame.size.height >= 2.f * extent;
}

static void border_draw(struct border* border, CGRect frame, struct settings* settings) {
  struct nine_slice ns;
  if (!border_nine_slice_style(border, settings, &ns)) {
    drawing_nine_slice_release(&border->nine_slice);
    border_draw_slow(border, frame, settings);
    return;
  }

  if (!border_nine_slice_fits(frame, &ns)) {
    border_draw_slow(border, frame, settings);
    return;
  }

  if (!border->nine_slice.corner[0]
      || !drawing_nine_slice_equal(&border->nine_slice.key, &ns)) {
    if (!drawing_nine_slice_build(&border->nine_slice, &ns)) {
      border_draw_slow(border, frame, settings);
      return;
    }
  }

  CGContextSaveGState(border->context);
  border->needs_redraw = false;

  border_clear(border, frame);
  drawing_draw_nine_slice(border->context, frame, &border->nine_slice);

  CGContextFlush(border->context);
  CGContextRestoreGState(border->context);
  SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
  SLSWindowThaw(border->cid, border->wid);
}

static struct layer_style border_layer_style(struct border* border, struct settings* settings) {
  struct layer_style style = { 0 };
  style.color = border->focused ? settings->active_window.layers[0]
                                : settings->inactive_window.layers[0];
  style.background = settings->background;
  style.show_background = settings->show_background
                          && settings->border_order != BORDER_ORDER_ABOVE;
  style.border_width = settings->border_width;
  style.outer_offset = settings->border_width + BORDER_PADDING;
  style.inner_radius = border->inner_radius;
  style.border_style = settings->border_style;
  style.corner_radius = settings->border_style == BORDER_STYLE_ROUND_UNIFORM
                        ? 9.f
                        : border->radius;
  style.truly_square = settings->border_style == BORDER_STYLE_SQUARE
                       && settings->border_order == BORDER_ORDER_ABOVE
                       && settings->border_width >= BORDER_TSMW;
  return style;
}

static bool border_layer_draw(struct border* border, CGRect frame, struct settings* settings) {
  struct layer_style style = border_layer_style(border, settings);
  if (!layer_border_update(border->layer, &style, frame.size)) {
    border->layer_failed = true;
    return false;
  }
  border->needs_redraw = false;
  return true;
}

static bool border_layer_style_expressible(struct border* border, struct settings* settings) {
  if (settings_shimmer_enabled(settings)) return false;
  if (settings->animation != 0 || settings->inactive_animation != 0) return false;
  if (border->window_state == BORDER_WINDOW_STATE_STACK
      && settings->stack_color_override) return false;
  if (border->window_state == BORDER_WINDOW_STATE_FLOATING
      && settings->floating_color_override) return false;
  if (border->window_state == BORDER_WINDOW_STATE_BSP
      && settings->bsp_color_override) return false;
  struct border_appearance appearance = border->focused
                                        ? settings->active_window
                                        : settings->inactive_window;
  if (appearance.layer_count != 1) return false;
  if (appearance.layers[0].stype == COLOR_STYLE_MULTI) return false;
  if (settings->blur_radius > 0.f
      || settings->active_blur_radius > 0.f
      || settings->inactive_blur_radius > 0.f) return false;
  return true;
}

void border_create_window(struct border* border, CGRect frame, bool unmanaged, bool hidpi) {
  pthread_mutex_lock(&border->mutex);
  int cid = border->cid;

  border->use_layer = false;
  border->layer = NULL;
  border->wid = 0;

  struct settings* settings = border_get_settings(border);
  if (!settings->force_cg
      && border_layer_style_expressible(border, settings)
      && !border->layer_failed
      && !unmanaged
      && !border->is_proxy) {
    uint32_t layer_wid = 0;
    border->layer = layer_border_create(cid, frame, unmanaged, hidpi, &layer_wid);
    if (border->layer) {
      border->wid = layer_wid;
      border->use_layer = true;
    }
  }

  if (!border->use_layer) {
    debug("Drawing this border into a backing store\n");
    border->wid = window_create(cid, frame, hidpi, unmanaged);
  }

  border->frame = frame;
  border->needs_redraw = true;
  border->border_blur_radius = UINT32_MAX;
  if (!border->use_layer) {
    border_recreate_context(border);
  }

  if (!border->sid) border->sid = window_space_id(cid, border->target_wid);
  window_send_to_space(cid, border->wid, border->sid);
  pthread_mutex_unlock(&border->mutex);
}

void border_update_internal(struct border* border, struct settings* settings) {
  if (border->external_proxy_wid) {
    border->stale_props = true;
    return;
  }
  if (!settings->enabled) {
    border_hide(border);
    return;
  }

  int cid = border->cid;
  // yabai can move a window between Spaces without delivering a Window Move
  // notification. Never make visibility or placement decisions from cached SID.
  uint64_t current_sid = window_space_id(cid, border->target_wid);
  if (current_sid && current_sid != border->sid) {
    border->sid = current_sid;
    if (border->wid) window_send_to_space(cid, border->wid, current_sid);
    if (border->background_wid)
      window_send_to_space(cid, border->background_wid, current_sid);
  }
  CGRect frame;
  if (!border_calculate_bounds(border, &frame, settings)) return;

  enum border_background_host background_host = border_background_host(
      settings,
      border->focused);
  float background_blur_radius = border_background_blur_radius(settings,
                                                               border->focused);
  if (background_host != BORDER_BACKGROUND_COMPANION) {
    border_destroy_background_window(border);
  }
  if (border->wid && background_host != BORDER_BACKGROUND_COMPANION) {
    border_set_blur_radius(border,
                           border->wid,
                           &border->border_blur_radius,
                           background_host == BORDER_BACKGROUND_BORDER
                           ? background_blur_radius
                           : 0.0f);
  }

  if (border->anim_origin_override) border->origin = border->anim_current_origin;
  // The sticky tag is not cached, no event announces a change of it
  bool refetch_props = border->stale_props || !border->wid;

  uint64_t tags = window_tags(cid, border->target_wid);
  border->sticky = tags & WINDOW_TAG_STICKY;
  if (!border->sticky && !is_space_visible(cid, border->sid)) {
    border_hide(border);
    return;
  }


  bool shown = false;
  SLSWindowIsOrderedIn(cid, border->target_wid, &shown);
  if (!shown && !border->is_proxy) {
    border_hide(border);
    return;
  } 

  if (refetch_props) {
    border->level = window_level(cid, border->target_wid);
    border->sub_level = window_sub_level(cid, border->target_wid);
    border->stale_props = false;
  }

  int level = border->level;
  int sub_level = border->sub_level;

  if (!border->wid) {
    border_create_window(border,
                         frame,
                         border->is_proxy,
                         settings->hidpi  );
  }

  bool background_placement_needed = false;
  if (background_host == BORDER_BACKGROUND_COMPANION && !border->is_proxy) {
    border_set_blur_radius(border,
                           border->wid,
                           &border->border_blur_radius,
                           0.0f);
    background_placement_needed = border_update_background(border,
                                                           border->target_bounds,
                                                           settings);
  } else if (background_host == BORDER_BACKGROUND_BORDER) {
    border_set_blur_radius(border,
                           border->wid,
                           &border->border_blur_radius,
                           background_blur_radius);
  }

  bool disabled_update = false;
  if (!CGRectEqualToRect(frame, border->frame)) {
    disabled_update = SLSDisableUpdate(cid) == kCGErrorSuccess;

    CFTypeRef frame_region = NULL;
    if (CGSNewRegionWithRect(&frame, &frame_region) != kCGErrorSuccess
        || !frame_region) {
      goto cleanup;
    }

    if (!border->use_layer)
      SLSWindowFreezeWithOptions(border->cid, border->wid, NULL);
    CGError shape_error = SLSSetWindowShape(border->cid,
                                            border->wid,
                                            border->origin.x,
                                            border->origin.y,
                                            frame_region);
    CFRelease(frame_region);
    if (shape_error != kCGErrorSuccess) {
      SLSWindowThaw(border->cid, border->wid);
      goto cleanup;
    }
    if (!border->use_layer) border_recreate_context(border);

    border->needs_redraw = true;
    border->frame = frame;
  }

  if (border->use_layer
      && (!border_layer_style_expressible(border, settings)
          || !border_layer_draw(border, frame, settings))) {
    border_destroy_window(border);
    border_create_window(border, frame, border->is_proxy, settings->hidpi);
  }

  if (!border->use_layer && border->needs_redraw) {
    border_draw(border, frame, settings);
  }

  CFTypeRef transaction = SLSTransactionCreate(cid);
  if (!transaction) {
    if (disabled_update) SLSReenableUpdate(cid);
    return;
  }
  SLSTransactionMoveWindowWithGroup(transaction, border->wid, border->origin);

  if (!border->is_proxy) {
    CGAffineTransform transform = CGAffineTransformIdentity;
    transform.tx = -border->origin.x;
    transform.ty = -border->origin.y;
    SLSTransactionSetWindowTransform(transaction,
                                     border->wid,
                                     0,
                                     0,
                                     transform   );
  }
  SLSTransactionSetWindowLevel(transaction, border->wid, level);
  SLSTransactionSetWindowSubLevel(transaction, border->wid, sub_level);
  SLSTransactionOrderWindow(transaction,
                            border->wid,
                            border_effective_order(settings),
                            border->target_wid      );

  bool update_background_placement = border_should_update_background_placement(
      border->background_wid != 0,
      border->animating,
      background_placement_needed);
  if (update_background_placement) {
    CGPoint background_origin = border->background_frame.origin;
    CGAffineTransform background_transform = CGAffineTransformIdentity;
    background_transform.tx = -background_origin.x;
    background_transform.ty = -background_origin.y;
    SLSTransactionMoveWindowWithGroup(transaction,
                                      border->background_wid,
                                      background_origin);
    SLSTransactionSetWindowTransform(transaction,
                                     border->background_wid,
                                     0,
                                     0,
                                     background_transform);
    SLSTransactionSetWindowLevel(transaction, border->background_wid, level);
    SLSTransactionSetWindowSubLevel(transaction,
                                   border->background_wid,
                                   sub_level);
    bool inactive_foreground = !border->focused
                               && settings->inactive_foreground;
    SLSTransactionOrderWindow(transaction,
                              border->background_wid,
                              inactive_foreground ? BORDER_ORDER_ABOVE
                                                  : BORDER_ORDER_BELOW,
                              inactive_foreground ? border->target_wid
                                                  : border_background_relative_wid(border,
                                                                                   settings));
  }
  SLSTransactionCommit(transaction, 0);
  CFRelease(transaction);

  uint64_t set_tags = (1ULL << 1) | (1ULL << 9);
  uint64_t clear_tags = 0;

  if (!border->tags_applied
      || refetch_props
      || border->applied_sticky != border->sticky) {
    if (border->sticky) {
      set_tags |= WINDOW_TAG_STICKY;
      clear_tags |= (1ULL << 45);
    }

    SLSSetWindowTags(cid, border->wid, &set_tags, 0x40);
    SLSClearWindowTags(cid, border->wid, &clear_tags, 0x40);

    border->tags_applied = true;
    border->applied_sticky = border->sticky;
  }

  if (update_background_placement) {
    SLSSetWindowTags(cid, border->background_wid, &set_tags, 0x40);
    SLSClearWindowTags(cid, border->background_wid, &clear_tags, 0x40);
  }

cleanup:
  if (disabled_update) SLSReenableUpdate(cid);
}

void border_init(struct border* border, int cid) {
  memset(border, 0, sizeof(struct border));
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&border->mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);
  animation_init(&border->animation);
  border->stale_props = true;
  if (cid) border->cid = cid;
  else border->cid = SLSMainConnectionID();
}

struct border* border_create() {
  struct border* border = malloc(sizeof(struct border));
  int cid = 0;
  SLSNewConnection(0, &cid);
  border_init(border, cid);
  return border;
}

void border_destroy_proxy(struct border* proxy) {
  if (!proxy) return;

  pthread_mutex_lock(&proxy->mutex);
  if (proxy->is_destroyed) {
    pthread_mutex_unlock(&proxy->mutex);
    return;
  }
  proxy->is_destroyed = true;
  ++proxy->update_generation;
  animation_stop(&proxy->animation);
  border_destroy_window(proxy);
  border_destroy_background_window(proxy);
  settings_destroy(&proxy->setting_override);
  pthread_mutex_unlock(&proxy->mutex);
  pthread_mutex_destroy(&proxy->mutex);
  free(proxy);
}

void border_destroy(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->is_destroyed) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  border->is_destroyed = true;
  ++border->update_generation;
  pthread_mutex_unlock(&border->mutex);

  border_hide(border);
  // Debounced updates retain only the raw border pointer. Keep this object
  // alive through the longest debounce window, then release resources.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    pthread_mutex_lock(&border->mutex);
    border_destroy_window(border);
    border_destroy_background_window(border);
    if (border->proxy) {
      struct border* proxy = border->proxy;
      border->proxy = NULL;
      border_destroy_proxy(proxy);
    }
    animation_stop(&border->animation);
    if (!border->is_proxy && border->cid != SLSMainConnectionID())
      SLSReleaseConnection(border->cid);
    settings_destroy(&border->setting_override);
    pthread_mutex_unlock(&border->mutex);
    pthread_mutex_destroy(&border->mutex);
    free(border);
  });
}

void border_move(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->is_destroyed || border->external_proxy_wid) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  struct settings* settings = border_get_settings(border);
  uint64_t new_sid = window_space_id(border->cid, border->target_wid);
  if (new_sid && new_sid != border->sid) {
    border->sid = new_sid;
    if (border->wid) window_send_to_space(border->cid, border->wid, new_sid);
    if (border->background_wid)
      window_send_to_space(border->cid, border->background_wid, new_sid);
  }
  CGRect window_frame = CGRectNull;
  if (SLSGetWindowBounds(border->cid,
                         border->target_wid,
                         &window_frame) != kCGErrorSuccess
      || !border_bounds_are_valid(window_frame)) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  float border_offset = settings->border_position == BORDER_POSITION_INSIDE
                        ? 0.0f
                        : border_max_extent(settings) + BORDER_PADDING;
  CGPoint origin = { .x = window_frame.origin.x - border_offset,
                     .y = window_frame.origin.y - border_offset };

  float expand = settings->border_position == BORDER_POSITION_INSIDE
                 ? 0.0f
                 : border_max_extent(settings) + BORDER_PADDING;
  CGSize expected_size = CGSizeMake(window_frame.size.width + 2.0f * expand,
                                    window_frame.size.height + 2.0f * expand);
  bool size_changed = !border->wid
                      || !CGSizeEqualToSize(expected_size,
                                            border->frame.size);
  bool background_resized
      = border->background_wid
        && border_background_host(settings, border->focused)
               == BORDER_BACKGROUND_COMPANION
        && !CGSizeEqualToSize(window_frame.size,
                              border->background_frame.size);
  if (size_changed || background_resized) {
    // ponytail: sync reshape; debounced updates starve under MOVE storms
    border_update_internal(border, settings);
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  CFTypeRef transaction = border->wid || border->background_wid
                          ? SLSTransactionCreate(border->cid)
                          : NULL;
  if (transaction) {
    if (border->wid) {
      SLSTransactionMoveWindowWithGroup(transaction, border->wid, origin);
    }
    if (border->background_wid
        && border_background_host(settings,
                                  border->focused) == BORDER_BACKGROUND_COMPANION) {
      SLSTransactionMoveWindowWithGroup(transaction,
                                        border->background_wid,
                                        window_frame.origin);
    }
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
  }
  border->target_bounds = window_frame;
  border->origin = origin;
  if (border->background_wid) {
    border->background_frame.origin = window_frame.origin;
  }
  pthread_mutex_unlock(&border->mutex);
}

// Collapse redraw bursts after a space switch without adding visible latency
// to normal focus and window-management updates.
#define DEBOUNCE_NORMAL_NS (5 * NSEC_PER_MSEC)
#define DEBOUNCE_SPACE_NS  (80 * NSEC_PER_MSEC)
#define SPACE_SETTLE_NS    (500 * NSEC_PER_MSEC)

static uint64_t g_space_change_deadline = 0;

void border_space_change_begin(void) {
  g_space_change_deadline = dispatch_time(DISPATCH_TIME_NOW, SPACE_SETTLE_NS);
}

void border_update(struct border* border, bool try_async) {
  if (!try_async) {
    pthread_mutex_lock(&border->mutex);
    if (!border->is_destroyed) {
      border_update_internal(border, border_get_settings(border));
    }
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  struct settings* settings = border_get_settings(border);
  __block struct settings settings_copy;
  settings_snapshot(&settings_copy, settings);
  pthread_mutex_lock(&border->mutex);
  uint64_t generation = ++border->update_generation;
  pthread_mutex_unlock(&border->mutex);

  uint64_t delay = dispatch_time(DISPATCH_TIME_NOW, 0) < g_space_change_deadline
                   ? DEBOUNCE_SPACE_NS
                   : DEBOUNCE_NORMAL_NS;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay),
                 dispatch_get_main_queue(), ^{
    pthread_mutex_lock(&border->mutex);
    if (!border->is_destroyed && generation == border->update_generation) {
      border_update_internal(border, &settings_copy);
    }
    pthread_mutex_unlock(&border->mutex);
  });
}

void border_hide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->wid || border->background_wid) {
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      if (border->wid) {
        SLSTransactionOrderWindow(transaction,
                                  border->wid,
                                  0,
                                  border->target_wid);
      }
      if (border->background_wid) {
        SLSTransactionOrderWindow(transaction,
                                  border->background_wid,
                                  0,
                                  border->target_wid);
      }
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }
  }
  border_schedule_background_eviction(border);
  pthread_mutex_unlock(&border->mutex);
}

void border_update_animating(struct border* border, float progress) {
  pthread_mutex_lock(&border->mutex);
  border->needs_redraw = true;
  struct settings* settings = border_get_settings(border);
  if (border->anim_origin_override) {
    float slide_progress = animation_ease(settings->animation_easing, progress);
    border->anim_current_origin.x = animation_lerp(border->anim_start_origin.x,
                                                   border->anim_end_origin.x,
                                                   slide_progress);
    border->anim_current_origin.y = animation_lerp(border->anim_start_origin.y,
                                                   border->anim_end_origin.y,
                                                   slide_progress);
  }
  if (border->animating && (border->anim_mode & ANIM_PULSE)) {
    border->anim_stroke_width = animation_pulse_width(
        border_appearance_extent(settings, border->focused),
        progress);
  } else {
    border->anim_stroke_width = border_appearance_extent(settings,
                                                         border->focused);
  }
  border_update_internal(border, settings);
  pthread_mutex_unlock(&border->mutex);
}
void border_unhide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->is_destroyed || border->external_proxy_wid) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  struct settings* settings = border_get_settings(border);
  border_update_internal(border, settings);
  pthread_mutex_unlock(&border->mutex);
}

void border_invalidate_props(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  border->stale_props = true;
  pthread_mutex_unlock(&border->mutex);
}
