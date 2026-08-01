#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/parse.c"

static void assert_close(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

int main(void) {
  struct settings settings = {};
  char glow_gradient[] =
      "active_color=glow(gradient(top_left=0xffff0000,bottom_right=0xff0000ff))";
  char* glow_gradient_arguments[] = { glow_gradient };

  uint32_t mask = parse_settings(&settings, 1, glow_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_GRADIENT);
  assert(settings.active_window.glow);
  assert(settings.active_window.gradient.direction == TL_TO_BR);
  assert(settings.active_window.gradient.color1 == 0xffff0000);
  assert(settings.active_window.gradient.color2 == 0xff0000ff);

  char reverse_glow_gradient[] =
      "active_color=glow(gradient(top_right=0xff00ff00,bottom_left=0xffffffff))";
  char* reverse_glow_gradient_arguments[] = { reverse_glow_gradient };
  mask = parse_settings(&settings, 1, reverse_glow_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.glow);
  assert(settings.active_window.gradient.direction == TR_TO_BL);

  char plain_gradient[] =
      "active_color=gradient(top_left=0xff010203,bottom_right=0xff040506)";
  char* plain_gradient_arguments[] = { plain_gradient };
  mask = parse_settings(&settings, 1, plain_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_GRADIENT);
  assert(!settings.active_window.glow);
  assert(settings.active_window.gradient.direction == TL_TO_BR);

  char solid_glow[] = "active_color=glow(0xff123456)";
  char* solid_glow_arguments[] = { solid_glow };
  mask = parse_settings(&settings, 1, solid_glow_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_SOLID);
  assert(settings.active_window.glow);
  assert(settings.active_window.color == 0xff123456);

  char plain_solid[] = "active_color=0xffabcdef";
  char* plain_solid_arguments[] = { plain_solid };
  mask = parse_settings(&settings, 1, plain_solid_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.stype == COLOR_STYLE_SOLID);
  assert(!settings.active_window.glow);
  assert(settings.active_window.color == 0xffabcdef);

  char background[] = "background_color=0x80112233";
  char* background_arguments[] = { background };
  mask = parse_settings(&settings, 1, background_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert(settings.background.color == 0x80112233);
  assert(border_background_color(&settings, true) == 0x80112233);
  assert(border_background_color(&settings, false) == 0x80112233);
  settings.border_order = BORDER_ORDER_ABOVE;
  assert(border_background_host(&settings, true) == BORDER_BACKGROUND_COMPANION);
  settings.border_order = BORDER_ORDER_BELOW;
  assert(border_background_host(&settings, false) == BORDER_BACKGROUND_BORDER);

  char border_host[] = "background_host=border";
  char* border_host_arguments[] = { border_host };
  mask = parse_settings(&settings, 1, border_host_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  settings.border_order = BORDER_ORDER_ABOVE;
  assert(border_background_host(&settings, true) == BORDER_BACKGROUND_BORDER);

  char companion_host[] = "background_host=companion";
  char* companion_host_arguments[] = { companion_host };
  mask = parse_settings(&settings, 1, companion_host_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  settings.border_order = BORDER_ORDER_BELOW;
  assert(border_background_host(&settings, false)
         == BORDER_BACKGROUND_COMPANION);

  char automatic_host[] = "background_host=auto";
  char* automatic_host_arguments[] = { automatic_host };
  mask = parse_settings(&settings, 1, automatic_host_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert(border_background_host(&settings, false) == BORDER_BACKGROUND_BORDER);

  enum border_background_mode previous_background_mode =
      settings.background_mode;
  char invalid_host[] = "background_host=invalid";
  char* invalid_host_arguments[] = { invalid_host };
  mask = parse_settings(&settings, 1, invalid_host_arguments);
  assert(mask == 0);
  assert(settings.background_mode == previous_background_mode);

  char blur[] = "blur_radius=12.5";
  char* blur_arguments[] = { blur };
  mask = parse_settings(&settings, 1, blur_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.blur_radius, 12.5f);

  char clamped_blur[] = "blur_radius=100";
  char* clamped_blur_arguments[] = { clamped_blur };
  mask = parse_settings(&settings, 1, clamped_blur_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.blur_radius, 50.0f);

  char invalid_blur[] = "blur_radius=-1";
  char* invalid_blur_arguments[] = { invalid_blur };
  mask = parse_settings(&settings, 1, invalid_blur_arguments);
  assert(mask == 0);
  assert_close(settings.blur_radius, 50.0f);

  settings.background.color = 0;
  settings.blur_radius = 0.0f;
  assert(border_background_host(&settings, true) == BORDER_BACKGROUND_NONE);

  struct settings state_settings = { .border_order = BORDER_ORDER_ABOVE };
  char inactive_background[] = "inactive_background_color=0x80123456";
  char* inactive_background_arguments[] = { inactive_background };
  mask = parse_settings(&state_settings, 1, inactive_background_arguments);
  assert(mask == BORDER_UPDATE_MASK_INACTIVE);
  assert(state_settings.inactive_background_override);
  assert(border_background_color(&state_settings, false) == 0x80123456);
  assert(border_background_color(&state_settings, true) == 0);

  char inactive_blur[] = "inactive_blur_radius=20";
  char* inactive_blur_arguments[] = { inactive_blur };
  mask = parse_settings(&state_settings, 1, inactive_blur_arguments);
  assert(mask == BORDER_UPDATE_MASK_INACTIVE);
  assert(state_settings.inactive_blur_override);
  assert_close(border_background_blur_radius(&state_settings, false), 20.0f);
  assert_close(border_background_blur_radius(&state_settings, true), 0.0f);
  assert(border_background_host(&state_settings, false)
         == BORDER_BACKGROUND_COMPANION);
  assert(border_background_host(&state_settings, true)
         == BORDER_BACKGROUND_NONE);

  char active_background[] = "active_background_color=0xffabcdef";
  char* active_background_arguments[] = { active_background };
  mask = parse_settings(&state_settings, 1, active_background_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(state_settings.active_background_override);
  assert(border_background_color(&state_settings, true) == 0xffabcdef);

  char active_blur[] = "active_blur_radius=7.5";
  char* active_blur_arguments[] = { active_blur };
  mask = parse_settings(&state_settings, 1, active_blur_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(state_settings.active_blur_override);
  assert_close(border_background_blur_radius(&state_settings, true), 7.5f);

  assert(!border_should_update_background_placement(true, true, false));
  assert(border_should_update_background_placement(true, true, true));
  assert(border_should_update_background_placement(true, false, false));
  assert(!border_should_update_background_placement(false, false, true));

  struct color_style previous_style = settings.active_window;
  char invalid[] = "active_color=glow(0xff123456)trailing";
  char* invalid_arguments[] = { invalid };
  mask = parse_settings(&settings, 1, invalid_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.active_window,
                &previous_style,
                sizeof(struct color_style)) == 0);

  float a, r, g, b;
  colors_mix(0xffff0000, 0xff0000ff, &a, &r, &g, &b);
  assert_close(a, 1.0f);
  assert_close(r, 0.5f);
  assert_close(g, 0.0f);
  assert_close(b, 0.5f);

  colors_mix(0x00ff0000, 0xff0000ff, &a, &r, &g, &b);
  assert_close(a, 0.5f);
  assert_close(r, 0.0f);
  assert_close(g, 0.0f);
  assert_close(b, 1.0f);

  puts("color, background, and blur parsing: ok");
  return 0;
}
