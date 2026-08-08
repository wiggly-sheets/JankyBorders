# JankyBorders

<img align="right" width="50%" src="images/screenshot.png" alt="Screenshot">

*JankyBorders* is a lightweight tool designed to add colored borders to
user windows on macOS 14.0+. It enhances the user experience by visually
highlighting the currently focused window without relying on the accessibility
API, thereby being faster than comparable tools.

## Usage
### Install
The binary can be made available by installing it through Homebrew:
```bash
brew tap FelixKratz/formulae
brew install borders
```

For a comprehensive overview of all available options and commands, consult the
man page: `man borders`. A rendered version of the man page is available in the
[Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page).

### Bootstrap with yabai
For example, if you are using `yabai`, you could add:
```bash
borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0 &
```
to the very end of your `yabairc`. This will start the borders with the
specified options along with yabai.

### Bootstrap with AeroSpace
You could add:
```toml
after-startup-command = [
  'exec-and-forget borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0'
]
```
to you `aerospace.toml`. This will start borders with the specified options
along with AeroSpace.

### Bootstrap with brew
If you want to run this as a separate service, you could use:
```bash
brew services start borders
```

### Configuring the appearance
You can either configure the appearance directly when starting the borders
process (as shown in "Bootstrap with yabai") or use a configuration file.
The appearance can be adapted at any point in time.

#### Drawing double borders

Use `double(outer,inner)` for focused and unfocused colors, and for the two
border widths:

```bash
borders \
  'active_color=double(glow(gradient(top_left=0xffff2d55,bottom_right=0xffffcc00)),glow(0xffffffff))' \
  'inactive_color=double(gradient(top_right=0xff2c2c2e,bottom_left=0xff636366),0xff8e8e93)' \
  'width=double(4.0,2.0)' \
  double_gap=1.0
```

The first value configures the outer border and the second configures the inner
border. `double_gap` adds transparent space between them and defaults to `0`.
A scalar `width` gives both layers the same width; when a color is not a
`double(...)`, only the first width is used. Each item inside `double(...)` is
an independent color style and accepts a solid color, `glow(...)`,
`gradient(...)`, or `glow(gradient(...))`. Wrap each layer separately; forms
such as `glow(double(...))` are not used.

Double borders support the existing `style`, `order`, `hidpi`, per-window
`apply-to`, background/blur, and all focus-animation options. During `pulse`,
both layers scale together while preserving the configured gap. During `ramp`,
each layer configured with glow ramps independently.

### Edge colors

Use `multi(...)` to color each border edge independently. All four edge colors
are required; corners use the color of their adjacent vertical edge.

```bash
borders 'active_color=multi(left=0xffff2d55,top=0xffffcc00,right=0xff64d2ff,bottom=0xff30d158)'
```

`multi(...)` currently accepts solid `0xAARRGGBB` colors and cannot be nested
inside `double(...)`.

### Inside-window borders

Use `position=inside` to draw a border within window bounds. This is useful
for maximized or edge-to-edge windows. Inside borders always render above the
application, so `position=inside` overrides `order=below`. Use
`position=outside` (default) to restore configured `order` behavior.

### Toggle borders

Run `borders toggle=on` to hide all borders; run same command again to restore
them with current appearance settings. This avoids changing and later restoring
`width`, colors, or other configuration.

### Shimmer

Animate active-border colors with a native replacement for janky-shimmer:

```bash
borders shimmer=0xffff0000,0xffffff00,0xff00ff00 shimmer_duration=3 shimmer_fps=30
```

`inactive_shimmer=` accepts a separate inactive palette. It is optional, so
inactive borders remain static unless configured. Palettes contain 2-16 solid
`0xAARRGGBB` colors; `shimmer_duration` defaults to 3 seconds and
`shimmer_fps` to 30 (maximum 120).

### yabai state colors

Configure colors for yabai state, then update individual windows from yabai
signals or any script:

```bash
borders stack_color=0xffff00ff floating_color=0xffff9500 bsp_color=0xff00d4ff
borders apply-to="$YABAI_WINDOW_ID" state=stack
```

States are `stack`, `floating`, `bsp`, and `none`. State colors override normal
and shimmer colors for that window. This signal bridge avoids polling yabai.

### Active-only mode

Use `active_only=on` to keep a border only for focused window. This reduces
backing-surface use; `inactive_color` has no visible effect. Default: `off`.

With adaptive `style=round`, windows reporting a corner radius of `0` or `1`
keep completely square corners across both layers. Double borders on normally
rounded windows remain concentric with the detected window radius.

### Persistent neighbouring borders

Use `visible_neighbouring_borders=on` to leave borders ordered during a Space
switch. macOS then keeps bordering windows visible while adjacent Spaces slide
past, instead of JankyBorders explicitly hiding them after its Space-change
event. `off` is default.

#### Animating focus changes
Focus animations are disabled by default. Enable one or more modes with a
comma-separated `animation=` value:

```bash
borders animation=fade,slide animation_duration=0.35 animation_easing=ease_out_expo
```

The available modes are:

- `fade` fades in the newly focused border.
- `ramp` grows its glow from zero to full and therefore requires an active
  color configured with `glow(...)`.
- `slide` moves the active border from the previously focused window to the
  newly focused window.
- `pulse` temporarily expands the active border. The expansion is strongest
  for thin borders and progressively gentler as the configured width grows.
- `none` disables focus animations and should be used by itself.

Comma-separated modes run simultaneously and share `animation_duration`,
which accepts a finite positive number and defaults to `0.25` seconds. The old
border immediately switches to its inactive color; only the newly focused
border animates. Focus changes while the primary mouse button is held are also
applied without animation.

Slide supports `animation_easing=linear`, `ease_in_expo`, `ease_out_expo`, or
`ease_in_out_expo`. Easing affects only slide; the other modes keep their
linear timing. For example, a glow ramp combined with pulse can be configured
as:

```bash
borders 'active_color=glow(0xffe2e2e3)' animation=ramp,pulse
```

#### Adding a window background and blur

With `order=above`, background fill and blur use a companion layer below each
application window so the border can remain above the application:

```bash
borders order=above background_color=0x80000000 blur_radius=20
```

The same options work with `order=below`; in that mode, fill and blur share the
border window to avoid creating a second overlay. Blur is disabled by default,
accepts values from `0` through `50`, and uses a private macOS API that may
affect performance.

The `background_host` option controls which window owns the fill and blur:

- `auto` (default) uses the border window with `order=below` and a target-sized
  companion window with `order=above`.
- `border` uses the original single-window implementation with either order.
  With `order=above`, the fill and blur are composited above the application.
- `companion` uses a target-sized companion window with either order. This keeps
  blur inside the application bounds, including with `order=below`.

```bash
# Original one-window behavior with an above-order border
borders order=above background_host=border blur_radius=20

# Boundary-constrained blur with a below-order border
borders order=below background_host=companion blur_radius=20
```

Focused and unfocused windows can override both defaults independently. For
example, this only adds fill and blur to inactive windows:

```bash
borders order=above \
  inactive_background_color=0x80000000 \
  inactive_blur_radius=20
```

The available overrides are `active_background_color`,
`inactive_background_color`, `active_blur_radius`, and
`inactive_blur_radius`. Windows without an override continue to use
`background_color` and `blur_radius`. Companion backgrounds are ordered out
immediately when their target is hidden, minimized, or on a non-visible Space.
Their backing resources are released after a ten-second grace period, unless
the target becomes visible again first.

#### Using a configuration file (Optional)
If the primary `borders` process is started without any arguments (or launched
as a service by brew), it will search for a file at
`~/.config/borders/bordersrc` and execute it on launch if found.

An example configuration file could look like this:
`~/.config/borders/bordersrc`
```bash
#!/bin/bash

options=(
	style=round
	width=6.0
	hidpi=off
	active_color=0xffe2e2e3
	inactive_color=0xff414550
	animation=fade,slide
	animation_duration=0.25
	animation_easing=ease_out_expo
)

borders "${options[@]}"
```

#### Updating the border properties during runtime
If a `borders` process is already running, invoking a new `borders` instance
with any combination of the available options will update the properties of
the already running instance.

## Documentation
Local documentation is available as `man borders` and as a rendered version in
the [Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page).
