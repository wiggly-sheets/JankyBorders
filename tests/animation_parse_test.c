#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/parse.c"

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

  char duration[] = "animation_duration=0.4";
  char* duration_arguments[] = { duration };
  mask = parse_settings(&settings, 1, duration_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.animation_duration == 0.4f);

  char easing[] = "animation_easing=ease_in_out_expo";
  char* easing_arguments[] = { easing };
  mask = parse_settings(&settings, 1, easing_arguments);
  assert(mask == BORDER_UPDATE_MASK_ANIMATION);
  assert(settings.animation_easing == ANIMATION_EASING_EASE_IN_OUT_EXPO);

  char invalid_combination[] = "animation=none,fade";
  char* invalid_combination_arguments[] = { invalid_combination };
  mask = parse_settings(&settings, 1, invalid_combination_arguments);
  assert(mask == 0);
  assert(settings.animation
         == (ANIM_FADE | ANIM_RAMP | ANIM_SLIDE | ANIM_PULSE));

  char invalid_duration[] = "animation_duration=0";
  char* invalid_duration_arguments[] = { invalid_duration };
  mask = parse_settings(&settings, 1, invalid_duration_arguments);
  assert(mask == 0);
  assert(settings.animation_duration == 0.4f);

  char infinite_duration[] = "animation_duration=inf";
  char* infinite_duration_arguments[] = { infinite_duration };
  mask = parse_settings(&settings, 1, infinite_duration_arguments);
  assert(mask == 0);
  assert(settings.animation_duration == 0.4f);

  puts("animation settings parsing: ok");
  return 0;
}
