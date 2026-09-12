# feat: add configurable focus-change animations

## Summary

Adds optional animations when focus moves from one window to another.

Animations are disabled by default and can be enabled independently or
combined with a comma-separated configuration value:

```sh
borders \
  animation=fade,slide \
  animation_duration=0.35 \
  animation_easing=ease_out_expo
```

The newly focused border is the only border that animates. The previously
focused border immediately switches to its inactive appearance.

## Motivation

Focus changes currently replace the active and inactive border appearances
immediately. This works well for responsiveness, but it can be difficult to
visually track focus across multiple windows or displays.

This change adds a small set of composable effects while preserving the
existing immediate behavior as the default.

## Animation modes

The following modes are available:

- `fade` fades in the newly focused border.
- `ramp` grows a configured glow from zero to its full blur radius.
- `slide` moves the active border from the previous window's origin to the
  newly focused window.
- `pulse` temporarily expands the active border before returning it to its
  configured width.
- `none` disables animations and must be used by itself.

Multiple modes run simultaneously and share one duration. Their order in the
comma-separated value does not affect the result:

```sh
borders animation=fade,slide,pulse animation_duration=0.3
```

`ramp` has a visible effect only when the active color has glow enabled.

## Configuration

### `animation=<modes>`

Accepts one or more comma-separated modes:

```text
none
fade
ramp
slide
pulse
```

The parser does not modify the input string, rejects unknown or empty modes,
and rejects combinations such as `animation=none,fade`.

The default is `none`.

### `animation_duration=<seconds>`

Sets the duration shared by the selected effects. The value must be finite
and greater than zero.

The default is:

```text
0.25
```

Each transition captures its duration when it starts. Updating the global or
per-window setting therefore does not change a transition already in flight.

### `animation_easing=<easing>`

Sets the interpolation curve used by `slide`:

```text
linear
ease_in_expo
ease_out_expo
ease_in_out_expo
```

The default is `linear`. Easing intentionally affects only slide positioning;
fade, ramp, and pulse retain linear progress.

## Focus transition behavior

On an animated focus change:

1. Every previously focused border is normalized and redrawn immediately with
   its inactive appearance.
2. The new border becomes the single focused border.
3. Its selected animation modes start together.
4. The shared ticker redraws the new border until the transition completes.
5. Animation-only state is cleared and the final border is drawn once in its
   normal active state.

Normalizing every previously focused border is deliberate. It restores the
single-focus invariant if an interrupted transition or duplicate event ever
leaves more than one border marked as focused.

Focus changes received while the primary mouse button is held skip animation.
This prevents focus effects from repeatedly playing while a window is being
dragged. The focus state and border appearance are still updated immediately.

## Implementation

### State model

Animation configuration is stored in `struct settings`. Runtime state is kept
per border and includes:

- Selected mode bits.
- Start time and captured duration.
- Current opacity and pulse width.
- Slide start, destination, and current origins.
- Whether an animated origin currently overrides the target window origin.

Keeping runtime state on each border allows transitions to be interrupted and
normalized without relying on one global active-border animation object. It
also allows per-window setting overrides to control mode, duration, easing,
width, and slide geometry.

### Ticker lifecycle

A `CFRunLoopTimer` drives active transitions at approximately 60 Hz on the
main run loop.

The ticker:

- Is created only when the first animation starts.
- Is reused while any border remains animated.
- Uses `CACurrentMediaTime()` for monotonic progress.
- Stops and releases itself when no animations remain.
- Draws one normalized final frame rather than drawing the completion frame
  twice.

### Fade and ramp

Fade applies the current animation progress as CGContext alpha while drawing
the newly active border.

Ramp scales the configured glow blur radius from zero to full strength. It
works with any active color style that has glow enabled and otherwise becomes
a no-op.

### Slide

Slide interpolates only the border window origin. The border is drawn using
the destination window's geometry while its window moves from the previous
origin to the new origin.

Start and destination offsets use the respective borders' effective width
settings, including per-window overrides. If either private
`SLSGetWindowBounds` query fails, the transition continues without the origin
override instead of using uninitialized geometry.

### Pulse

Pulse uses a triangular expansion envelope: the border reaches its maximum
width halfway through the transition and returns to its configured width at
completion.

The expansion is adaptive:

```text
maximum expansion = 6 / sqrt(max(configured width, 1))
```

This makes pulse clearly visible for one-pixel borders without making already
thick borders disproportionately large. The pulse width also feeds the drawing
inset, so the rendered geometry changes along with the CGContext line width.

### Interruption and cleanup

Starting a new transition cancels stale animation state on every old focused
border before starting the new active animation.

Non-animated focus changes, including mouse-held changes, also clear mode,
duration, origin override, and alpha state. This prevents a previous fade,
slide, or pulse from leaking into later redraws.

## Tests

Adds focused regression tests for:

- Parsing all modes from a comma-separated value.
- Preserving the input configuration string.
- Rejecting `none` combined with another mode.
- Rejecting zero and infinite durations without changing the previous value.
- Parsing exponential easing values.
- Animating only the newly focused border.
- Immediately redrawing the old border as inactive.
- Running multiple selected modes simultaneously.
- Suppressing animation while the primary mouse button is held.
- Recovering when multiple borders are incorrectly marked focused.
- Respecting per-window mode, duration, and width overrides.
- Calculating the correct slide start and destination origins.
- Safely handling failed window-bounds queries.
- Adaptive pulse width behavior for thin and thick borders.
- Easing boundary values and interpolation.
- Clearing runtime state and stopping after completion.
- Avoiding a duplicate animated redraw on the final frame.

Validated with:

```sh
make test
make debug
make
```

A sanitizer-enabled build and the focused animation tests also complete
without ASan or UBSan failures.

## Documentation

Updates the README, scdoc source, and generated man page with:

- Available modes and their behavior.
- Comma-separated simultaneous composition.
- Duration validation and default value.
- Supported slide easing curves.
- Mouse-held focus behavior.
- The requirement that ramp be paired with a glow-enabled color.
- Configuration examples.

## Compatibility

Animations default to disabled, so existing configurations retain their
current focus behavior.

The change adds new configuration keys without removing or renaming existing
keys. Existing border styles and colors continue through the same drawing
paths when no animation is active.

## Known limitations

- Effects run simultaneously; sequential animation chains are not supported.
- Slide translates the destination border geometry and does not morph between
  different window sizes or corner radii.
- Easing currently applies only to slide.
- Ramp requires a glow-enabled active color.
- Focus changes while the primary mouse button is held intentionally do not
  animate, including ordinary click-to-focus transitions received before the
  button is released.

## Visual verification

Tested with:

- [ ] `fade`
- [ ] `ramp` with a solid glow
- [ ] `ramp` with a gradient glow
- [ ] `slide` between similarly sized windows
- [ ] `slide` between differently sized windows
- [ ] `pulse` with a one-pixel border
- [ ] `pulse` with a thick border
- [ ] Combined `fade,slide,pulse`
- [ ] All supported slide easing curves
- [ ] Rapid focus changes across three or more windows
- [ ] Focus changes while dragging a window
- [ ] Per-window width and animation overrides
- [ ] Rounded, uniform rounded, and square borders
- [ ] Borders ordered above and below target windows

<!-- Add a short recording showing the individual and combined modes here. -->
