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

static bool parse_color(struct color_style* style, const char* token) {
  if (parse_gradient(style,
                     token,
                     "=glow(gradient(top_left=0x%x,bottom_right=0x%x))%n",
                     TL_TO_BR,
                     true)
      || parse_gradient(style,
                        token,
                        "=glow(gradient(top_right=0x%x,bottom_left=0x%x))%n",
                        TR_TO_BL,
                        true)
      || parse_gradient(style,
                        token,
                        "=gradient(top_left=0x%x,bottom_right=0x%x)%n",
                        TL_TO_BR,
                        false)
      || parse_gradient(style,
                        token,
                        "=gradient(top_right=0x%x,bottom_left=0x%x)%n",
                        TR_TO_BL,
                        false)
      || parse_solid(style, token, "=glow(0x%x)%n", true)
      || parse_solid(style, token, "=0x%x%n", false)) {
    return true;
  }

  printf("[?] Borders: Invalid color argument color%s\n", token);

  return false;
}

uint32_t parse_settings(struct settings* settings, int count, char** arguments) {
  static char active_color[] = "active_color";
  static char inactive_color[] = "inactive_color";
  static char background_color[] = "background_color";
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
    else if (str_starts_with(arguments[i], background_color)) {
      struct color_style bg;
      if (parse_color(&bg, arguments[i] + strlen(background_color))) {
        if (bg.stype == COLOR_STYLE_GRADIENT) {
          printf("[?] Borders: background_color does not support gradients\n");
        } else {
          settings->background = bg;
          update_mask |= BORDER_UPDATE_MASK_ALL;
          settings->show_background = settings->background.color & 0xff000000;
        }
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
    else if (sscanf(arguments[i], "width=%f", &settings->border_width) == 1) {
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (sscanf(arguments[i], "order=%c", &order) == 1) {
      if (order == 'a') settings->border_order = BORDER_ORDER_ABOVE;
      else settings->border_order = BORDER_ORDER_BELOW;
      update_mask |= BORDER_UPDATE_MASK_ALL;
    }
    else if (sscanf(arguments[i], "style=%c", &settings->border_style) == 1) {
      update_mask |= BORDER_UPDATE_MASK_ALL;
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
    else if (strcmp(arguments[i], "ax_focus=on") == 0) {
      settings->ax_focus = true;
      update_mask |= BORDER_UPDATE_MASK_SETTING;
    }
    else if (strcmp(arguments[i], "ax_focus=off") == 0) {
      settings->ax_focus = false;
      update_mask |= BORDER_UPDATE_MASK_SETTING;
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
