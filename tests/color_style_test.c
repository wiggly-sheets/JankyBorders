#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/parse.c"

static void assert_close(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

static uint32_t parse_one(struct settings* settings, char* argument) {
  return parse_settings(settings, 1, &argument);
}

static void test_single_and_double_color_styles(void) {
  struct settings settings = {};

  assert(parse_one(
             &settings,
             "active_color=glow(gradient(top_left=0xffff0000,"
             "bottom_right=0xff0000ff))")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_GRADIENT);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TL_TO_BR);
  assert(settings.active_window.layers[0].gradient.color1 == 0xffff0000);
  assert(settings.active_window.layers[0].gradient.color2 == 0xff0000ff);

  assert(parse_one(
             &settings,
             "active_color=glow(gradient(top_right=0xff00ff00,"
             "bottom_left=0xffffffff))")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TR_TO_BL);

  assert(parse_one(
             &settings,
             "active_color=gradient(top_left=0xff010203,"
             "bottom_right=0xff040506)")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_GRADIENT);
  assert(!settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].gradient.direction == TL_TO_BR);

  assert(parse_one(&settings, "active_color=glow(0xff123456)")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_SOLID);
  assert(settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].color == 0xff123456);

  assert(parse_one(&settings, "active_color=0xffabcdef")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_SOLID);
  assert(!settings.active_window.layers[0].glow);
  assert(settings.active_window.layers[0].color == 0xffabcdef);

  assert(parse_one(
             &settings,
             "active_color=multi(left=0xffff0000,top=0xff00ff00,"
             "right=0xff0000ff,bottom=0xffffffff)")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_window.layer_count == 1);
  assert(settings.active_window.layers[0].stype == COLOR_STYLE_MULTI);
  assert(settings.active_window.layers[0].multi.left == 0xffff0000);
  assert(settings.active_window.layers[0].multi.top == 0xff00ff00);
  assert(settings.active_window.layers[0].multi.right == 0xff0000ff);
  assert(settings.active_window.layers[0].multi.bottom == 0xffffffff);

  struct border_appearance previous_multi = settings.active_window;
  assert(parse_one(&settings,
                   "active_color=multi(left=0xffff0000,top=0xff00ff00)")
         == 0);
  assert(memcmp(&settings.active_window,
                &previous_multi,
                sizeof(struct border_appearance)) == 0);

  assert(parse_one(
             &settings,
             "active_color=double(glow(gradient(top_left=0xffff0000,"
             "bottom_right=0xff0000ff)),gradient(top_right=0xff00ff00,"
             "bottom_left=0xffffffff))")
         == BORDER_UPDATE_MASK_ACTIVE);
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

  assert(parse_one(&settings,
                   "inactive_color=double(glow(0xff111111),0xff222222)")
         == BORDER_UPDATE_MASK_INACTIVE);
  assert(settings.inactive_window.layer_count == 2);
  assert(settings.inactive_window.layers[0].stype == COLOR_STYLE_SOLID);
  assert(settings.inactive_window.layers[0].glow);
  assert(settings.inactive_window.layers[0].color == 0xff111111);
  assert(settings.inactive_window.layers[1].stype == COLOR_STYLE_SOLID);
  assert(!settings.inactive_window.layers[1].glow);
  assert(settings.inactive_window.layers[1].color == 0xff222222);

  struct border_appearance previous_active = settings.active_window;
  assert(parse_one(&settings,
                   "active_color=double(0xffff0000,0xff00ff00)trailing")
         == 0);
  assert(memcmp(&settings.active_window,
                &previous_active,
                sizeof(struct border_appearance)) == 0);
  assert(parse_one(&settings,
                   "active_color=glow(double(0xffff0000,0xff00ff00))")
         == 0);
  assert(memcmp(&settings.active_window,
                &previous_active,
                sizeof(struct border_appearance)) == 0);
  assert(parse_one(&settings, "active_color=glow(0xff123456)trailing") == 0);
  assert(memcmp(&settings.active_window,
                &previous_active,
                sizeof(struct border_appearance)) == 0);
}

static void test_positions_state_and_toggles(void) {
  struct settings settings = {};

  assert(parse_one(&settings, "position=inside")
         == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.border_position == BORDER_POSITION_INSIDE);

  assert(parse_one(&settings, "position=outside")
         == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.border_position == BORDER_POSITION_OUTSIDE);

  assert(parse_one(&settings, "stack_color=0xffff00ff")
         == BORDER_UPDATE_MASK_ALL);
  assert(settings.stack_color_override);
  assert(settings.stack_color == 0xffff00ff);

  assert(parse_one(&settings, "state=stack")
         == BORDER_UPDATE_MASK_WINDOW_STATE);
  assert(settings.window_state == BORDER_WINDOW_STATE_STACK);

  assert(parse_one(&settings, "active_only=on")
         == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.active_only);

  assert(parse_one(&settings, "style=none")
         == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.border_style == BORDER_STYLE_NONE);

  assert(parse_one(&settings, "toggle=on")
         == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(settings.enabled);
  assert(parse_one(&settings, "toggle=on")
         == BORDER_UPDATE_MASK_RECREATE_ALL);
  assert(!settings.enabled);
}

static void test_shimmer_settings(void) {
  struct settings settings = {};

  assert(parse_one(&settings,
                   "shimmer=0xffff0000,0xffffff00,0xff00ff00")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.shimmer_color_count == 3);
  assert(settings.shimmer_colors[0] == 0xffff0000);
  assert(settings.shimmer_colors[2] == 0xff00ff00);

  assert(parse_one(&settings, "inactive_shimmer=0xff111111,0xff222222")
         == BORDER_UPDATE_MASK_INACTIVE);
  assert(settings.inactive_shimmer_color_count == 2);

  assert(parse_one(&settings, "shimmer_duration=2.5")
         == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.shimmer_duration, 2.5f);

  assert(parse_one(&settings, "shimmer_fps=24") == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.shimmer_fps, 24.0f);
}

static void test_border_width_and_extent(void) {
  struct settings settings = {
    .active_window = { .layer_count = 2 },
    .inactive_window = { .layer_count = 2 },
  };

  assert(parse_one(&settings, "width=double(5.5,2.25)")
         == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.border_width, 5.5f);
  assert_close(settings.inner_border_width, 2.25f);

  assert(parse_one(&settings, "double_gap=1.5") == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.double_border_gap, 1.5f);
  assert_close(border_appearance_extent(&settings, true), 9.25f);
  assert_close(border_appearance_extent(&settings, false), 9.25f);
  assert_close(border_max_extent(&settings), 9.25f);

  assert(parse_one(&settings, "width=3") == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.border_width, 3.0f);
  assert_close(settings.inner_border_width, 3.0f);

  assert(parse_one(&settings, "width=double(4,-1)") == 0);
  assert_close(settings.border_width, 3.0f);
  assert_close(settings.inner_border_width, 3.0f);
}

static void test_background_colors_blur_and_hosts(void) {
  struct settings settings = {};

  assert(parse_one(&settings, "background_color=0x80112233")
         == BORDER_UPDATE_MASK_ALL);
  assert(settings.background.color == 0x80112233);
  assert(border_background_color(&settings, true) == 0x80112233);
  assert(border_background_color(&settings, false) == 0x80112233);

  struct color_style previous_background = settings.background;
  assert(parse_one(&settings,
                   "background_color=gradient(top_left=0xff112233,"
                   "bottom_right=0xff445566)")
         == 0);
  assert(memcmp(&settings.background,
                &previous_background,
                sizeof(struct color_style)) == 0);

  settings.border_order = BORDER_ORDER_ABOVE;
  assert(border_background_host(&settings, true)
         == BORDER_BACKGROUND_COMPANION);
  settings.border_order = BORDER_ORDER_BELOW;
  assert(border_background_host(&settings, false) == BORDER_BACKGROUND_BORDER);

  assert(parse_one(&settings, "background_host=border")
         == BORDER_UPDATE_MASK_ALL);
  settings.border_order = BORDER_ORDER_ABOVE;
  assert(border_background_host(&settings, true) == BORDER_BACKGROUND_BORDER);

  assert(parse_one(&settings, "background_host=companion")
         == BORDER_UPDATE_MASK_ALL);
  settings.border_order = BORDER_ORDER_BELOW;
  assert(border_background_host(&settings, false)
         == BORDER_BACKGROUND_COMPANION);

  assert(parse_one(&settings, "background_host=auto")
         == BORDER_UPDATE_MASK_ALL);
  assert(border_background_host(&settings, false) == BORDER_BACKGROUND_BORDER);

  enum border_background_mode previous_background_mode =
      settings.background_mode;
  assert(parse_one(&settings, "background_host=invalid") == 0);
  assert(settings.background_mode == previous_background_mode);

  assert(parse_one(&settings, "blur_radius=12.5") == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.blur_radius, 12.5f);
  assert(border_background_host(&settings, false) == BORDER_BACKGROUND_BORDER);

  assert(parse_one(&settings, "blur_radius=100") == BORDER_UPDATE_MASK_ALL);
  assert_close(settings.blur_radius, 50.0f);

  assert(parse_one(&settings, "blur_radius=-1") == 0);
  assert_close(settings.blur_radius, 50.0f);

  settings.background.color = 0;
  settings.blur_radius = 0.0f;
  assert(border_background_host(&settings, true) == BORDER_BACKGROUND_NONE);
}

static void test_state_background_overrides(void) {
  struct settings settings = { .border_order = BORDER_ORDER_ABOVE };

  assert(parse_one(&settings, "inactive_background_color=0x80123456")
         == BORDER_UPDATE_MASK_INACTIVE);
  assert(settings.inactive_background_override);
  assert(border_background_color(&settings, false) == 0x80123456);
  assert(border_background_color(&settings, true) == 0);

  assert(parse_one(&settings, "inactive_blur_radius=20")
         == BORDER_UPDATE_MASK_INACTIVE);
  assert(settings.inactive_blur_override);
  assert_close(border_background_blur_radius(&settings, false), 20.0f);
  assert_close(border_background_blur_radius(&settings, true), 0.0f);
  assert(border_background_host(&settings, false)
         == BORDER_BACKGROUND_COMPANION);
  assert(border_background_host(&settings, true) == BORDER_BACKGROUND_NONE);

  assert(parse_one(&settings, "active_background_color=0xffabcdef")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_background_override);
  assert(border_background_color(&settings, true) == 0xffabcdef);

  assert(parse_one(&settings, "active_blur_radius=7.5")
         == BORDER_UPDATE_MASK_ACTIVE);
  assert(settings.active_blur_override);
  assert_close(border_background_blur_radius(&settings, true), 7.5f);

  assert(border_should_update_background_placement(true, false, false));
  assert(!border_should_update_background_placement(false, true, true));
}

static void test_colors_mix(void) {
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
}

int main(void) {
  test_single_and_double_color_styles();
  test_positions_state_and_toggles();
  test_shimmer_settings();
  test_border_width_and_extent();
  test_background_colors_blur_and_hosts();
  test_state_background_overrides();
  test_colors_mix();
  puts("single/double color, background, and blur parsing: ok");
  return 0;
}
