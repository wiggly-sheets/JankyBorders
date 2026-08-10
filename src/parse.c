#include "parse.h"
#include "border.h"
#include "hashtable.h"

static bool str_starts_with(char* string, char* prefix) {
  if (!string || !prefix) return false;
  if (strlen(string) < strlen(prefix)) return false;
  if (strncmp(prefix, string, strlen(prefix)) == 0) return true;
  return false;
}

static bool token_equals(const char* token, size_t token_length, const char* value) {
  return strlen(value) == token_length
         && strncmp(token, value, token_length) == 0;
}

static bool parse_list(struct table* list, char* token) {
  uint32_t token_len = strlen(token) + 1;
  char copy[token_len];
  memcpy(copy, token, token_len);

  char* name;
  char* cursor = copy;
  bool entry_found = false;

  table_clear(list);
  while((name = strsep(&cursor, ","))) {
    if (strlen(name) > 0) {
      _table_add(list, name, strlen(name) + 1, (void*)true);
      entry_found = true;
    }
  }
  return entry_found;
}

static bool parse_gradient(struct color_style* style,
                           const char* token,
                           const char* format,
                           int direction,
                           bool glow) {
  uint32_t color1;
  uint32_t color2;
  int consumed = 0;
  if (sscanf(token, format, &color1, &color2, &consumed) != 2
      || consumed != (int)strlen(token)) {
    return false;
  }

  style->stype = COLOR_STYLE_GRADIENT;
  style->glow = glow;
  style->gradient.direction = direction;
  style->gradient.color1 = color1;
  style->gradient.color2 = color2;
  return true;
}

static bool parse_solid(struct color_style* style,
                        const char* token,
                        const char* format,
                        bool glow) {
  uint32_t color;
  int consumed = 0;
  if (sscanf(token, format, &color, &consumed) != 1
      || consumed != (int)strlen(token)) {
    return false;
  }

  style->stype = COLOR_STYLE_SOLID;
  style->glow = glow;
  style->color = color;
  return true;
}

static bool parse_multi(struct color_style* style, const char* token) {
  uint32_t left, top, right, bottom;
  int consumed = 0;
  if (sscanf(token,
             "multi(left=0x%x,top=0x%x,right=0x%x,bottom=0x%x)%n",
             &left, &top, &right, &bottom, &consumed) != 4
      || consumed != (int)strlen(token)) {
    return false;
  }

  style->stype = COLOR_STYLE_MULTI;
  style->glow = false;
  style->multi.left = left;
  style->multi.top = top;
  style->multi.right = right;
  style->multi.bottom = bottom;
  return true;
}

static bool parse_color_style(struct color_style* style, const char* token) {
  if (parse_multi(style, token)
      || parse_gradient(style,
                     token,
                     "glow(gradient(top_left=0x%x,bottom_right=0x%x))%n",
                     TL_TO_BR,
                     true)
      || parse_gradient(style,
                        token,
                        "glow(gradient(top_right=0x%x,bottom_left=0x%x))%n",
                        TR_TO_BL,
                        true)
      || parse_gradient(style,
                        token,
                        "gradient(top_left=0x%x,bottom_right=0x%x)%n",
                        TL_TO_BR,
                        false)
      || parse_gradient(style,
                        token,
                        "gradient(top_right=0x%x,bottom_left=0x%x)%n",
                        TR_TO_BL,
                        false)
      || parse_solid(style, token, "glow(0x%x)%n", true)
      || parse_solid(style, token, "0x%x%n", false)) {
    return true;
  }

  return false;
}

static bool parse_color_style_span(struct color_style* style,
                                   const char* start,
                                   const char* end) {
  if (!start || !end || end <= start) return false;
  size_t length = (size_t)(end - start);
  char token[length + 1];
  memcpy(token, start, length);
  token[length] = '\0';
  return parse_color_style(style, token);
}

static const char* find_double_separator(const char* start, const char* end) {
  int depth = 0;
  for (const char* cursor = start; cursor < end; ++cursor) {
    if (*cursor == '(') {
      depth++;
    } else if (*cursor == ')') {
      if (depth == 0) return NULL;
      depth--;
    } else if (*cursor == ',' && depth == 0) {
      return cursor;
    }
  }
  return NULL;
}

static bool parse_color(struct border_appearance* appearance,
                        const char* token) {
  if (!token || token[0] != '=') return false;

  const char* value = token + 1;
  const char* end = value + strlen(value);
  struct border_appearance parsed = {};
  if (strncmp(value, "double(", strlen("double(")) == 0
      && end > value
      && end[-1] == ')') {
    const char* outer_start = value + strlen("double(");
    const char* inner_end = end - 1;
    const char* separator = find_double_separator(outer_start, inner_end);
    if (separator
        && parse_color_style_span(&parsed.layers[0], outer_start, separator)
        && parse_color_style_span(&parsed.layers[1], separator + 1, inner_end)) {
      parsed.layer_count = 2;
      *appearance = parsed;
      return true;
    }
  } else if (parse_color_style_span(&parsed.layers[0], value, end)) {
    parsed.layer_count = 1;
    *appearance = parsed;
    return true;
  }

  printf("[?] Borders: Invalid color argument color%s\n", token);
  return false;
}

static bool parse_background_color(struct color_style* background,
                                   const char* token,
                                   const char* name) {
  struct border_appearance appearance;
  if (!parse_color(&appearance, token)) return false;
  if (appearance.layer_count != 1
      || appearance.layers[0].stype != COLOR_STYLE_SOLID) {
    printf("[?] Borders: %s only supports a single solid color\n", name);
    return false;
  }
  *background = appearance.layers[0];
  return true;
}

static bool parse_blur_radius(float* result,
                              const char* token,
                              const char* name) {
  float blur_radius;
  int consumed = 0;
  if (sscanf(token, "%f%n", &blur_radius, &consumed) != 1
      || consumed != (int)strlen(token)
      || !isfinite(blur_radius)
      || blur_radius < 0.0f) {
    printf("[?] Borders: %s must be finite and non-negative\n", name);
    return false;
  }
  if (blur_radius > 50.0f) {
    printf("[?] Borders: %s capped at 50\n", name);
    blur_radius = 50.0f;
  }
  *result = blur_radius;
  return true;
}

static bool parse_non_negative_float(float* result,
                                     const char* token,
                                     const char* name) {
  float value;
  int consumed = 0;
  if (sscanf(token, "%f%n", &value, &consumed) != 1
      || consumed != (int)strlen(token)
      || !isfinite(value)
      || value < 0.0f) {
    printf("[?] Borders: %s must be finite and non-negative\n", name);
    return false;
  }
  *result = value;
  return true;
}

static bool parse_shimmer_colors(uint32_t* colors,
                                 uint32_t* color_count,
                                 const char* value,
                                 const char* name) {
  uint32_t parsed[SHIMMER_MAX_COLORS];
  uint32_t count = 0;
  char copy[strlen(value) + 1];
  strcpy(copy, value);
  char* cursor = copy;
  char* token;
  while ((token = strsep(&cursor, ","))) {
    uint32_t color;
    int consumed = 0;
    if (count == SHIMMER_MAX_COLORS
        || sscanf(token, "0x%x%n", &color, &consumed) != 1
        || consumed != (int)strlen(token)) {
      printf("[?] Borders: %s requires 2-%d colors in 0xAARRGGBB format\n",
             name, SHIMMER_MAX_COLORS);
      return false;
    }
    parsed[count++] = color;
  }
  if (count < 2) {
    printf("[?] Borders: %s requires 2-%d colors in 0xAARRGGBB format\n",
           name, SHIMMER_MAX_COLORS);
    return false;
  }
  memcpy(colors, parsed, sizeof(parsed));
  *color_count = count;
  return true;
}

static bool parse_state_color(uint32_t* color, bool* override,
                              const char* value, const char* name) {
  int consumed = 0;
  if (sscanf(value, "0x%x%n", color, &consumed) != 1
      || consumed != (int)strlen(value)) {
    printf("[?] Borders: %s requires a solid 0xAARRGGBB color\n", name);
    return false;
  }
  *override = true;
  return true;
}

static bool parse_animation_modes(const char* value, int* result) {
  int modes = 0;
  bool none = false;
  while (value) {
    const char* separator = strchr(value, ',');
    size_t length = separator ? (size_t)(separator - value) : strlen(value);
    if (token_equals(value, length, "none")) none = true;
    else if (token_equals(value, length, "fade")) modes |= ANIM_FADE;
    else if (token_equals(value, length, "ramp")) modes |= ANIM_RAMP;
    else if (token_equals(value, length, "slide")) modes |= ANIM_SLIDE;
    else if (token_equals(value, length, "pulse")) modes |= ANIM_PULSE;
    else return false;
    value = separator ? separator + 1 : NULL;
  }
  if (none && modes) return false;
  *result = modes;
  return true;
}

static bool parse_widths(struct settings* settings, const char* token) {
  float outer;
  float inner;
  int consumed = 0;
  if (sscanf(token, "double(%f,%f)%n", &outer, &inner, &consumed) == 2
      && consumed == (int)strlen(token)) {
    if (!isfinite(outer) || !isfinite(inner) || outer <= 0.0f || inner <= 0.0f) {
      printf("[?] Borders: double border widths must be finite and greater than zero\n");
      return false;
    }
    settings->border_width = outer;
    settings->inner_border_width = inner;
    return true;
  }

  float width;
  consumed = 0;
  if (sscanf(token, "%f%n", &width, &consumed) != 1
      || consumed != (int)strlen(token)
      || !isfinite(width)
      || width <= 0.0f) {
    printf("[?] Borders: width must be finite and greater than zero\n");
    return false;
  }
  settings->border_width = width;
  settings->inner_border_width = width;
  return true;
}

uint32_t parse_settings(struct settings* settings, int count, char** arguments) {
  static char active_color[] = "active_color";
  static char inactive_color[] = "inactive_color";
  static char background_color[] = "background_color";
  static char active_background_color[] = "active_background_color";
  static char inactive_background_color[] = "inactive_background_color";
  static char blacklist[] = "blacklist=";
  static char whitelist[] = "whitelist=";

  char order = 'a';
  uint32_t update_mask = 0;
  for (int i = 0; i < count; i++) {
    if (str_starts_with(arguments[i], active_color)) {
      if (parse_color(&settings->active_window,
                                 arguments[i] + strlen(active_color))) {
        update_mask |= BORDER_UPDATE_MASK_ACTIVE;
      }
    }
    else  if (str_starts_with(arguments[i], inactive_color)) {
      if (parse_color(&settings->inactive_window,
                                 arguments[i] + strlen(inactive_color))) {
        update_mask |= BORDER_UPDATE_MASK_INACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], active_background_color)) {
      if (parse_background_color(&settings->active_background,
                                 arguments[i] + strlen(active_background_color),
                                 active_background_color)) {
        settings->active_background_override = true;
        update_mask |= BORDER_UPDATE_MASK_ACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], inactive_background_color)) {
      if (parse_background_color(&settings->inactive_background,
                                 arguments[i] + strlen(inactive_background_color),
                                 inactive_background_color)) {
        settings->inactive_background_override = true;
        update_mask |= BORDER_UPDATE_MASK_INACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], background_color)) {
      if (parse_background_color(&settings->background,
                                 arguments[i] + strlen(background_color),
                                 background_color)) {
        update_mask |= BORDER_UPDATE_MASK_ALL;
      }
    }
    else if (str_starts_with(arguments[i], blacklist)) {
      settings->blacklist_enabled = parse_list(&settings->blacklist,
                                               arguments[i]
                                               + strlen(blacklist));
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (str_starts_with(arguments[i], whitelist)) {
      settings->whitelist_enabled = parse_list(&settings->whitelist,
                                               arguments[i]
                                               + strlen(whitelist));
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (str_starts_with(arguments[i], "width=")) {
      if (parse_widths(settings, arguments[i] + strlen("width="))) {
        update_mask |= BORDER_UPDATE_MASK_ALL;
      }
    }
    else if (str_starts_with(arguments[i], "double_gap=")) {
      if (parse_non_negative_float(&settings->double_border_gap,
                                   arguments[i] + strlen("double_gap="),
                                   "double_gap")) {
        update_mask |= BORDER_UPDATE_MASK_ALL;
      }
    }
    else if (sscanf(arguments[i], "order=%c", &order) == 1) {
      if (order == 'a') settings->border_order = BORDER_ORDER_ABOVE;
      else settings->border_order = BORDER_ORDER_BELOW;
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (strcmp(arguments[i], "position=inside") == 0) {
      settings->border_position = BORDER_POSITION_INSIDE;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (strcmp(arguments[i], "position=outside") == 0) {
      settings->border_position = BORDER_POSITION_OUTSIDE;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (str_starts_with(arguments[i], "background_host=")) {
      const char* host = arguments[i] + strlen("background_host=");
      if (strcmp(host, "auto") == 0) {
        settings->background_mode = BORDER_BACKGROUND_AUTO;
      } else if (strcmp(host, "border") == 0) {
        settings->background_mode = BORDER_BACKGROUND_FORCE_BORDER;
      } else if (strcmp(host, "companion") == 0) {
        settings->background_mode = BORDER_BACKGROUND_FORCE_COMPANION;
      } else {
        printf("[?] Borders: Invalid background host '%s'\n", host);
        continue;
      }
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (sscanf(arguments[i], "style=%c", &settings->border_style) == 1) {
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (str_starts_with(arguments[i], "animation=")) {
      const char* value = arguments[i] + strlen("animation=");
      int animation = 0;
      bool has_none = false;
      bool has_mode = false;
      while (value) {
        const char* separator = strchr(value, ',');
        size_t token_length = separator
                              ? (size_t)(separator - value)
                              : strlen(value);
        if (token_equals(value, token_length, "none")) {
          has_none = true;
        } else if (token_equals(value, token_length, "fade")) {
          animation |= ANIM_FADE;
          has_mode = true;
        } else if (token_equals(value, token_length, "ramp")) {
          animation |= ANIM_RAMP;
          has_mode = true;
        } else if (token_equals(value, token_length, "slide")) {
          animation |= ANIM_SLIDE;
          has_mode = true;
        } else if (token_equals(value, token_length, "pulse")) {
          animation |= ANIM_PULSE;
          has_mode = true;
        } else {
          printf("[?] Borders: Invalid animation value '%.*s'\n",
                 (int)token_length,
                 value);
          animation = -1;
          break;
        }
        value = separator ? separator + 1 : NULL;
      }
      if (has_none && has_mode) {
        printf("[?] Borders: animation=none cannot be combined with other modes\n");
        animation = -1;
      }
      if (animation >= 0) {
        settings->animation = animation;
        update_mask |= BORDER_UPDATE_MASK_ANIMATION;
      }
    }
    else if (str_starts_with(arguments[i], "shimmer=")) {
      if (parse_shimmer_colors(settings->shimmer_colors,
                               &settings->shimmer_color_count,
                               arguments[i] + strlen("shimmer="),
                               "shimmer")) {
        update_mask |= BORDER_UPDATE_MASK_ACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], "inactive_animation=")) {
      int animation;
      if (parse_animation_modes(arguments[i] + strlen("inactive_animation="),
                                &animation)) {
        settings->inactive_animation = animation;
        update_mask |= BORDER_UPDATE_MASK_ANIMATION;
      } else {
        printf("[?] Borders: Invalid inactive_animation value\n");
      }
    }
    else if (str_starts_with(arguments[i], "inactive_shimmer=")) {
      if (parse_shimmer_colors(settings->inactive_shimmer_colors,
                               &settings->inactive_shimmer_color_count,
                               arguments[i] + strlen("inactive_shimmer="),
                               "inactive_shimmer")) {
        update_mask |= BORDER_UPDATE_MASK_INACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], "shimmer_duration=")) {
      float duration;
      if (sscanf(arguments[i], "shimmer_duration=%f", &duration) == 1
          && duration > 0.0f && isfinite(duration)) {
        settings->shimmer_duration = duration;
        update_mask |= BORDER_UPDATE_MASK_ALL;
      } else {
        printf("[?] Borders: shimmer_duration must be finite and greater than zero\n");
      }
    }
    else if (str_starts_with(arguments[i], "shimmer_fps=")) {
      float fps;
      if (sscanf(arguments[i], "shimmer_fps=%f", &fps) == 1
          && fps > 0.0f && isfinite(fps) && fps <= 120.0f) {
        settings->shimmer_fps = fps;
        update_mask |= BORDER_UPDATE_MASK_ALL;
      } else {
        printf("[?] Borders: shimmer_fps must be finite, 0-120\n");
      }
    }
    else if (str_starts_with(arguments[i], "stack_color=")) {
      if (parse_state_color(&settings->stack_color, &settings->stack_color_override,
                            arguments[i] + strlen("stack_color="), "stack_color"))
        update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (str_starts_with(arguments[i], "floating_color=")) {
      if (parse_state_color(&settings->floating_color, &settings->floating_color_override,
                            arguments[i] + strlen("floating_color="), "floating_color"))
        update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (str_starts_with(arguments[i], "bsp_color=")) {
      if (parse_state_color(&settings->bsp_color, &settings->bsp_color_override,
                            arguments[i] + strlen("bsp_color="), "bsp_color"))
        update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (str_starts_with(arguments[i], "state=")) {
      const char* state = arguments[i] + strlen("state=");
      if (strcmp(state, "none") == 0) settings->window_state = BORDER_WINDOW_STATE_NONE;
      else if (strcmp(state, "stack") == 0) settings->window_state = BORDER_WINDOW_STATE_STACK;
      else if (strcmp(state, "floating") == 0) settings->window_state = BORDER_WINDOW_STATE_FLOATING;
      else if (strcmp(state, "bsp") == 0) settings->window_state = BORDER_WINDOW_STATE_BSP;
      else { printf("[?] Borders: Invalid state '%s'\n", state); continue; }
      update_mask |= BORDER_UPDATE_MASK_WINDOW_STATE;
    }
    else if (str_starts_with(arguments[i], "animation_duration=")) {
      float duration;
      if (sscanf(arguments[i], "animation_duration=%f", &duration) == 1
          && duration > 0.0f
          && isfinite(duration)) {
        settings->animation_duration = duration;
        update_mask |= BORDER_UPDATE_MASK_ANIMATION;
      } else {
        printf("[?] Borders: animation_duration must be finite and greater than zero\n");
      }
    }
    else if (str_starts_with(arguments[i], "animation_easing=")) {
      const char* easing = arguments[i] + strlen("animation_easing=");
      if (strcmp(easing, "linear") == 0) {
        settings->animation_easing = ANIMATION_EASING_LINEAR;
      } else if (strcmp(easing, "ease_in_expo") == 0) {
        settings->animation_easing = ANIMATION_EASING_EASE_IN_EXPO;
      } else if (strcmp(easing, "ease_out_expo") == 0) {
        settings->animation_easing = ANIMATION_EASING_EASE_OUT_EXPO;
      } else if (strcmp(easing, "ease_in_out_expo") == 0) {
        settings->animation_easing = ANIMATION_EASING_EASE_IN_OUT_EXPO;
      } else {
        printf("[?] Borders: Invalid animation easing '%s'\n", easing);
        continue;
      }
      update_mask |= BORDER_UPDATE_MASK_ANIMATION;
    }
    else if (strcmp(arguments[i], "hidpi=on") == 0) {
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
      settings->hidpi = true;
    }
    else if (strcmp(arguments[i], "hidpi=off") == 0) {
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
      settings->hidpi = false;
    }
    else if (str_starts_with(arguments[i], "active_blur_radius=")) {
      if (parse_blur_radius(&settings->active_blur_radius,
                            arguments[i] + strlen("active_blur_radius="),
                            "active_blur_radius")) {
        settings->active_blur_override = true;
        update_mask |= BORDER_UPDATE_MASK_ACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], "inactive_blur_radius=")) {
      if (parse_blur_radius(&settings->inactive_blur_radius,
                            arguments[i] + strlen("inactive_blur_radius="),
                            "inactive_blur_radius")) {
        settings->inactive_blur_override = true;
        update_mask |= BORDER_UPDATE_MASK_INACTIVE;
      }
    }
    else if (str_starts_with(arguments[i], "blur_radius=")) {
      if (parse_blur_radius(&settings->blur_radius,
                            arguments[i] + strlen("blur_radius="),
                            "blur_radius")) {
        update_mask |= BORDER_UPDATE_MASK_ALL;
      }
    }
    else if (strcmp(arguments[i], "ax_focus=on") == 0) {
      settings->ax_focus = true;
      update_mask |= BORDER_UPDATE_MASK_SETTING;
    }
    else if (strcmp(arguments[i], "ax_focus=off") == 0) {
      settings->ax_focus = false;
      update_mask |= BORDER_UPDATE_MASK_SETTING;
    }
    else if (strcmp(arguments[i], "active_only=on") == 0) {
      settings->active_only = true;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (strcmp(arguments[i], "active_only=off") == 0) {
      settings->active_only = false;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (strcmp(arguments[i], "inactive_foreground=on") == 0) {
      settings->inactive_foreground = true;
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (strcmp(arguments[i], "inactive_foreground=off") == 0) {
      settings->inactive_foreground = false;
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (strcmp(arguments[i], "visible_neighbouring_borders=on") == 0) {
      settings->visible_neighbouring_borders = true;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (strcmp(arguments[i], "visible_neighbouring_borders=off") == 0) {
      settings->visible_neighbouring_borders = false;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (strcmp(arguments[i], "toggle=on") == 0) {
      settings->enabled = !settings->enabled;
      update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
    }
    else if (sscanf(arguments[i], "apply-to=%d", &settings->apply_to) == 1) {
      update_mask |= BORDER_UPDATE_MASK_SETTING;
    }
    else {
      printf("[?] Borders: Invalid argument '%s'\n", arguments[i]);
    }
  }
  return update_mask;
}
