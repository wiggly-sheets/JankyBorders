#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/parse.c"

static void assert_close(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

int main(void) {
  struct settings settings = {
    .animation_duration = 0.25f,
  };
  char animation[] = "animation=fade,ramp,slide,pulse";
  char original[sizeof(animation)];
  memcpy(original, animation, sizeof(animation));
  char* animation_arguments[] = { animation };

  uint32_t mask = parse_settings(&settings, 1, animation_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.animation
         == (ANIM_FADE | ANIM_RAMP | ANIM_SLIDE | ANIM_PULSE));
  assert(memcmp(animation, original, sizeof(animation)) == 0);

  char invalid_combination[] = "animation=none,fade";
  char* invalid_combination_arguments[] = { invalid_combination };
  mask = parse_settings(&settings, 1, invalid_combination_arguments);
  assert(mask == 0);
  assert(settings.animation
         == (ANIM_FADE | ANIM_RAMP | ANIM_SLIDE | ANIM_PULSE));

  char none[] = "animation=none";
  char* none_arguments[] = { none };
  mask = parse_settings(&settings, 1, none_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.animation == 0);

  char duration[] = "animation_duration=0.4";
  char* duration_arguments[] = { duration };
  mask = parse_settings(&settings, 1, duration_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert_close(settings.animation_duration, 0.4f);

  char duration_high[] = "animation_duration=60";
  char* duration_high_arguments[] = { duration_high };
  mask = parse_settings(&settings, 1, duration_high_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert_close(settings.animation_duration, 60.0f);

  char duration_over_high[] = "animation_duration=60.1";
  char* duration_over_high_arguments[] = { duration_over_high };
  mask = parse_settings(&settings, 1, duration_over_high_arguments);
  assert(mask == 0);
  assert_close(settings.animation_duration, 60.0f);

  char duration_under_low[] = "animation_duration=0.001";
  char* duration_under_low_arguments[] = { duration_under_low };
  mask = parse_settings(&settings, 1, duration_under_low_arguments);
  assert(mask == 0);
  assert_close(settings.animation_duration, 60.0f);

  char duration_zero[] = "animation_duration=0";
  char* duration_zero_arguments[] = { duration_zero };
  mask = parse_settings(&settings, 1, duration_zero_arguments);
  assert(mask == 0);
  assert_close(settings.animation_duration, 60.0f);

  char duration_trailing[] = "animation_duration=1second";
  char* duration_trailing_arguments[] = { duration_trailing };
  mask = parse_settings(&settings, 1, duration_trailing_arguments);
  assert(mask == 0);
  assert_close(settings.animation_duration, 60.0f);

  char infinite_duration[] = "animation_duration=inf";
  char* infinite_duration_arguments[] = { infinite_duration };
  mask = parse_settings(&settings, 1, infinite_duration_arguments);
  assert(mask == 0);
  assert_close(settings.animation_duration, 60.0f);

  char easing[] = "animation_easing=ease_in_out_expo";
  char* easing_arguments[] = { easing };
  mask = parse_settings(&settings, 1, easing_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.animation_easing == ANIMATION_EASING_EASE_IN_OUT_EXPO);

  char invalid_easing[] = "animation_easing=nope";
  char* invalid_easing_arguments[] = { invalid_easing };
  mask = parse_settings(&settings, 1, invalid_easing_arguments);
  assert(mask == 0);
  assert(settings.animation_easing == ANIMATION_EASING_EASE_IN_OUT_EXPO);

  char inactive_fade[] = "inactive_animation=fade";
  char* inactive_fade_arguments[] = { inactive_fade };
  mask = parse_settings(&settings, 1, inactive_fade_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.inactive_animation == ANIM_FADE);

  char invalid_inactive_animation[] = "inactive_animation=none,fade";
  char* invalid_inactive_animation_arguments[] = { invalid_inactive_animation };
  mask = parse_settings(&settings,
                        1,
                        invalid_inactive_animation_arguments);
  assert(mask == 0);
  assert(settings.inactive_animation == ANIM_FADE);

  char inactive_animation[] = "inactive_animation=fade,slide";
  char* inactive_animation_arguments[] = { inactive_animation };
  mask = parse_settings(&settings, 1, inactive_animation_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.inactive_animation == (ANIM_FADE | ANIM_SLIDE));

  char inactive_none[] = "inactive_animation=none";
  char* inactive_none_arguments[] = { inactive_none };
  mask = parse_settings(&settings, 1, inactive_none_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.inactive_animation == 0);

  char fade_duration_zero[] = "fade_duration=0";
  char* fade_duration_zero_arguments[] = { fade_duration_zero };
  mask = parse_settings(&settings, 1, fade_duration_zero_arguments);
  assert(mask == BORDER_UPDATE_MASK_SETTING);
  assert_close(settings.fade_duration, 0.0f);

  char fade_duration[] = "fade_duration=1.5";
  char* fade_duration_arguments[] = { fade_duration };
  mask = parse_settings(&settings, 1, fade_duration_arguments);
  assert(mask == BORDER_UPDATE_MASK_SETTING);
  assert_close(settings.fade_duration, 1.5f);

  char fade_duration_over_high[] = "fade_duration=60.1";
  char* fade_duration_over_high_arguments[] = { fade_duration_over_high };
  mask = parse_settings(&settings, 1, fade_duration_over_high_arguments);
  assert(mask == 0);
  assert_close(settings.fade_duration, 1.5f);

  puts("animation settings parsing: ok");
  return 0;
}
