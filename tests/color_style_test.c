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
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_GRADIENT);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TL_TO_BR);
  assert(settings.active_window.layers[0].gradient.color1 == 0xffff0000);
  assert(settings.active_window.layers[0].gradient.color2 == 0xff0000ff);

  char reverse_glow_gradient[] =
      "active_color=glow(gradient(top_right=0xff00ff00,bottom_left=0xffffffff))";
  char* reverse_glow_gradient_arguments[] = { reverse_glow_gradient };
  mask = parse_settings(&settings, 1, reverse_glow_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TR_TO_BL);

  char plain_gradient[] =
      "active_color=gradient(top_left=0xff010203,bottom_right=0xff040506)";
  char* plain_gradient_arguments[] = { plain_gradient };
  mask = parse_settings(&settings, 1, plain_gradient_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_GRADIENT);
  assert(!settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TL_TO_BR);

  char solid_glow[] = "active_color=glow(0xff123456)";
  char* solid_glow_arguments[] = { solid_glow };
  mask = parse_settings(&settings, 1, solid_glow_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_SOLID);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].color == 0xff123456);

  char plain_solid[] = "active_color=0xffabcdef";
  char* plain_solid_arguments[] = { plain_solid };
  mask = parse_settings(&settings, 1, plain_solid_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_SOLID);
  assert(!settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].color == 0xffabcdef);

  char active_double[] =
      "active_color=double(glow(gradient(top_left=0xffff0000,bottom_right=0xff0000ff)),gradient(top_right=0xff00ff00,bottom_left=0xffffffff))";
  char* active_double_arguments[] = { active_double };
  mask = parse_settings(&settings, 1, active_double_arguments);
  assert(mask == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 2);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_GRADIENT);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TL_TO_BR);
  assert(settings.active_window.layers[0].gradient.color1 == 0xffff0000);
  assert(settings.active_window.layers[0].gradient.color2 == 0xff0000ff);
  assert(settings.active_window.layers[1].stype == COLOR_STYLE_GRADIENT);
  assert(!settings.active_window.layers[1].glow);
  assert(settings.active_window.layers[1].gradient.direction == TR_TO_BL);
  assert(settings.active_window.layers[1].gradient.color1 == 0xff00ff00);
  assert(settings.active_window.layers[1].gradient.color2 == 0xffffffff);

  char inactive_double[] =
      "inactive_color=double(glow(0xff111111),0xff222222)";
  char* inactive_double_arguments[] = { inactive_double };
  mask = parse_settings(&settings, 1, inactive_double_arguments);
  assert(mask == BORDER_UPDATE_MASK_INACTIVE);
  assert(settings.inactive_window.layer_count == 2);
  assert(settings.inactive_window.layers[0].stype == COLOR_STYLE_SOLID);
  assert(settings.inactive_window.layers[0].glow);
  assert(settings.inactive_window.layers[0].color == 0xff111111);
  assert(settings.inactive_window.layers[1].stype == COLOR_STYLE_SOLID);
  assert(!settings.inactive_window.layers[1].glow);
  assert(settings.inactive_window.layers[1].color == 0xff222222);

  char double_width[] = "width=double(5.5,2.25)";
  char* double_width_arguments[] = { double_width };
  mask = parse_settings(&settings, 1, double_width_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.border_width, 5.5f);
  assert_close(settings.inner_border_width, 2.25f);

  char double_gap[] = "double_gap=1.5";
  char* double_gap_arguments[] = { double_gap };
  mask = parse_settings(&settings, 1, double_gap_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.double_border_gap, 1.5f);
  assert_close(border_appearance_extent(&settings, true), 9.25f);
  assert_close(border_appearance_extent(&settings, false), 9.25f);
  assert_close(border_max_extent(&settings), 9.25f);

  char single_width[] = "width=3";
  char* single_width_arguments[] = { single_width };
  mask = parse_settings(&settings, 1, single_width_arguments);
  assert(mask == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.border_width, 3.0f);
  assert_close(settings.inner_border_width, 3.0f);

  float previous_outer_width = settings.border_width;
  float previous_inner_width = settings.inner_border_width;
  char invalid_double_width[] = "width=double(4,-1)";
  char* invalid_double_width_arguments[] = { invalid_double_width };
  mask = parse_settings(&settings, 1, invalid_double_width_arguments);
  assert(mask == 0);
  assert_close(settings.border_width, previous_outer_width);
  assert_close(settings.inner_border_width, previous_inner_width);

  struct border_appearance previous_double = settings.active_window;
  char invalid_double_color[] =
      "active_color=double(0xffff0000,0xff00ff00)trailing";
  char* invalid_double_color_arguments[] = { invalid_double_color };
  mask = parse_settings(&settings, 1, invalid_double_color_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.active_window,
                &previous_double,
                sizeof(struct border_appearance)) == 0);

  char wrapped_double[] =
      "active_color=glow(double(0xffff0000,0xff00ff00))";
  char* wrapped_double_arguments[] = { wrapped_double };
  mask = parse_settings(&settings, 1, wrapped_double_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.active_window,
                &previous_double,
                sizeof(struct border_appearance)) == 0);

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

  struct border_appearance previous_style = settings.active_window;
  char invalid[] = "active_color=glow(0xff123456)trailing";
  char* invalid_arguments[] = { invalid };
  mask = parse_settings(&settings, 1, invalid_arguments);
  assert(mask == 0);
  assert(memcmp(&settings.active_window,
                &previous_style,
                sizeof(struct border_appearance)) == 0);

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

  puts("single/double color, background, and blur parsing: ok");
  return 0;
}
