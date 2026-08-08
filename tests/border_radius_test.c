#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/border.h"

static void assert_close(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

static void assert_radii(int32_t detected_radius,
                         float expected_outer,
                         float expected_inner) {
  struct border border = {};
  border_set_detected_radius(&border, detected_radius);
  assert_close(border.radius, expected_outer);
  assert_close(border.inner_radius, expected_inner);
}

int main(void) {
  assert_radii(0, 0.0f, BORDER_TSMN);
  assert_radii(1, 0.0f, BORDER_TSMN);
  assert_radii(12, 12.0f, 13.0f);
  assert_radii(17, 17.0f, 18.0f);
  assert_radii(-1, BORDER_DEFAULT_RADIUS, BORDER_DEFAULT_RADIUS + 1.0f);

  assert_close(border_layer_corner_radius(0.0f, 1.0f), 0.0f);
  assert_close(border_layer_corner_radius(0.0f, 8.0f), 0.0f);
  assert_close(border_layer_corner_radius(12.0f, 1.0f), 13.0f);
  assert_close(border_layer_corner_radius(12.0f, 8.0f), 20.0f);

  struct settings outside = { .border_order = BORDER_ORDER_BELOW,
                              .border_position = BORDER_POSITION_OUTSIDE };
  assert(border_effective_order(&outside) == BORDER_ORDER_BELOW);
  outside.border_position = BORDER_POSITION_INSIDE;
  assert(border_effective_order(&outside) == BORDER_ORDER_ABOVE);

  puts("adaptive border radius: ok");
  return 0;
}
