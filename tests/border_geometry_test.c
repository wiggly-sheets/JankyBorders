#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/border.c"

struct settings g_settings;
struct table g_windows;

static CGRect reported_bounds;
static CGError reported_bounds_error;

CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect* bounds) {
  (void)cid;
  (void)wid;
  if (reported_bounds_error != kCGErrorSuccess) return reported_bounds_error;
  *bounds = reported_bounds;
  return kCGErrorSuccess;
}

static void assert_close(CGFloat actual, CGFloat expected) {
  assert(fabs(actual - expected) < 0.0001);
}

static void assert_rect(CGRect actual, CGRect expected) {
  assert_close(actual.origin.x, expected.origin.x);
  assert_close(actual.origin.y, expected.origin.y);
  assert_close(actual.size.width, expected.size.width);
  assert_close(actual.size.height, expected.size.height);
}

int main(void) {
  reported_bounds = CGRectMake(100.0, 200.0, 300.0, 400.0);
  struct settings settings = {
    .active_window = { .layer_count = 2 },
    .inactive_window = { .layer_count = 1 },
    .border_width = 4.0f,
    .inner_border_width = 2.0f,
    .double_border_gap = 1.0f,
    .border_position = BORDER_POSITION_OUTSIDE,
  };
  struct border border = {
    .cid = 1,
    .target_wid = 42,
    .inner_radius = 10.0f,
  };
  CGRect frame = CGRectNull;

  assert(border_calculate_bounds(&border, &frame, &settings));
  assert_rect(frame, CGRectMake(0.0, 0.0, 330.0, 430.0));
  assert_close(border.origin.x, 85.0);
  assert_close(border.origin.y, 185.0);
  assert_rect(border.drawing_bounds,
              CGRectMake(15.0, 15.0, 300.0, 400.0));

  settings.border_position = BORDER_POSITION_INSIDE;
  assert(border_calculate_bounds(&border, &frame, &settings));
  assert_rect(frame, CGRectMake(0.0, 0.0, 300.0, 400.0));
  assert_close(border.origin.x, 100.0);
  assert_close(border.origin.y, 200.0);
  assert_rect(border.drawing_bounds,
              CGRectMake(0.0, 0.0, 300.0, 400.0));

  float outer_center;
  float inner_center;
  border_double_layer_centers(false,
                              4.0f,
                              2.0f,
                              1.0f,
                              &outer_center,
                              &inner_center);
  assert_close(outer_center, 5.0f);
  assert_close(inner_center, 1.0f);
  border_double_layer_centers(true,
                              4.0f,
                              2.0f,
                              1.0f,
                              &outer_center,
                              &inner_center);
  assert_close(outer_center, -2.0f);
  assert_close(inner_center, -6.0f);

  CGRect cached_bounds = border.target_bounds;
  reported_bounds_error = kCGErrorFailure;
  assert(!border_calculate_bounds(&border, &frame, &settings));
  assert_rect(border.target_bounds, cached_bounds);

  reported_bounds_error = kCGErrorSuccess;
  reported_bounds.size.width = 0.0;
  assert(!border_calculate_bounds(&border, &frame, &settings));
  assert_rect(border.target_bounds, cached_bounds);

  border.is_proxy = true;
  border.target_bounds = CGRectMake(1.0, 2.0, NAN, 50.0);
  assert(!border_calculate_bounds(&border, &frame, &settings));

  puts("inside/outside border geometry and bounds validation: ok");
  return 0;
}
