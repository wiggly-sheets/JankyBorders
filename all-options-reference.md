# JankyBorders extended-options reference

This is a companion to the upstream README and `borders(1)` man page, not a
replacement for them. It has two deliberately separate scopes:

1. capabilities added by the local branch stack that are not available in
   upstream JankyBorders; and
2. controls and runtime behavior present in upstream `main` but absent from,
   unsupported by, or underexplained in its published documentation.

Last verified: 2026-08-01 against upstream commit
[`a7297ca` (v1.9.0)](https://github.com/FelixKratz/JankyBorders/commit/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e).
The upstream parser was compared directly with the
[README](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/README.md)
and [man-page source](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/docs/borders.1.scd).

## Quick index

| Control or capability | Upstream status | Default | Runtime change |
| --- | --- | --- | --- |
| Double borders: `double(...)`, `width=double(...)`, `double_gap` | Local `codex/double-borders` branch only; not upstream | Disabled; gap `0` | Immediate redraw of affected borders |
| Combined glow gradients: `glow(gradient(...))` | Local `feature/glow-gradient` branch and descendants only; not upstream | Disabled | Immediate redraw of matching focus state |
| Focus animations: `animation`, `animation_duration`, `animation_easing` | Local `feature/border-animation` branch and descendants only; not upstream | Disabled; `0.25` seconds; `linear` | Used on later focus changes |
| Focus-aware background and blur controls | Local `codex/background-blur` branch and descendants only; not upstream | Transparent; blur `0` | Immediate redraw and layer update |
| `background_host=auto\|border\|companion` | Local only; not upstream | `auto` | Immediate redraw and layer update |
| `renderer=layer\|cg` | Local only; not upstream | `layer` (auto-falls back to CG when blur is active) | Recreates all borders |
| `inactive_foreground=on\|off` | Local only; not upstream | `off` | Immediate redraw and layer update |
| Adaptive squaring for detected radius `0` or `1` | Local `codex/adaptive-square-radius` branch and descendants only; not upstream | Automatic | Applied when radius geometry is detected |
| `order=below` / `order=above` | Implemented, explicitly unsupported and undocumented | `below` | Immediate redraw of all borders |
| `apply-to=<window-id>` | Implemented, undocumented | None | Applies an override to one existing border |
| `style=uniform` | Implemented; named only in the man page's value glossary | `round` | Immediate redraw of all borders |
| `-v`, `--version` | Implemented, undocumented | N/A | Prints and exits |
| `-h`, `--help` | Implemented, undocumented | N/A | Prints a man-page pointer and exits |
| Border-window shadow suppression | Always enabled, no option | Shadow disabled | Requires border-window recreation to differ; no supported way to do so |
| yabai animation-proxy tracking | Built in, no option | Enabled | Event driven |
| macOS 26 per-window radius detection | Built in, no option | Automatic | Applied when a border is created |
| Sticky-window following | Built in, no option | Automatic | Refreshed during border updates |
| Ignore-cycle window filtering | Built in, no option | Enabled | Applied when deciding whether a window is suitable |

## Local branch capabilities

The branches are stacked in this order: `feature/glow-gradient`,
`feature/border-animation`, `codex/background-blur`,
`codex/adaptive-square-radius`, and `codex/double-borders`. Each subsection
below describes only the functionality introduced at that layer.

### Double borders

This feature is implemented on the local `codex/double-borders` branch. It is
not present in the upstream v1.9.0 snapshot used for the rest of this file.

#### Syntax

```sh
borders \
  'active_color=double(glow(gradient(top_left=0xffff2d55,bottom_right=0xffffcc00)),glow(0xffffffff))' \
  'inactive_color=double(gradient(top_right=0xff2c2c2e,bottom_left=0xff636366),0xff8e8e93)' \
  'width=double(4.0,2.0)' \
  double_gap=1.0
```

The arguments containing parentheses normally need to be quoted in a shell.

- The first color is the **outer** layer and the second is the **inner** layer.
- Each layer is a complete color style. The outer and inner layers can
  independently use a solid color, glow, gradient, or glowing gradient.
- `width=double(outer,inner)` sets the two layer widths independently.
- A scalar such as `width=4` assigns that width to both layers. A single-color
  border renders only the ordinary width; the stored inner width is relevant
  only when the selected focused or unfocused color is `double(...)`.
- `double_gap=<float>` places transparent space between the layers. It defaults
  to `0`, so the two strokes touch.

The inner layer begins at the normal window edge. The gap and outer layer
extend outward from it, so the complete double-border extent is:

```text
inner width + transparent gap + outer width
```

Backing-window sizing uses the larger of the focused and unfocused extents.
That prevents clipping when only one focus state uses a double color. See the
local extent helpers in [`border.h`](../src/border.h) and the double-layer
renderer in [`border.c`](../src/border.c).

#### Per-layer color styles

Each position inside `double(outer,inner)` accepts the same syntax as an
ordinary border color:

```text
0xAARRGGBB
glow(0xAARRGGBB)
gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB)
gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB)
glow(gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB))
glow(gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB))
```

The two positions do not need to use the same style. For example:

```sh
# Outer glow-gradient, inner solid glow
'active_color=double(glow(gradient(top_left=0xffff0000,bottom_right=0xff0000ff)),glow(0xffffffff))'

# Outer plain gradient, inner plain solid
'inactive_color=double(gradient(top_right=0xff303030,bottom_left=0xff707070),0xffa0a0a0)'
```

Active and inactive appearances are also independent. Together, the two
settings provide four independently styled slots: active outer, active inner,
inactive outer, and inactive inner.

Styles are wrapped independently inside `double(...)`. There is deliberately
no automatic outer wrapper, so `glow(double(...))` is invalid; use
`double(glow(...),glow(...))` when both layers should glow.

#### Valid values and limitations

- Both double widths must be finite and greater than zero.
- The gap must be finite and non-negative.
- A malformed value is rejected without partially replacing the previous
  setting.
- Nested `double(...)` expressions and wrappers around the complete double
  expression are rejected.
- A double wrapper cannot be used as a background color.

These rules are enforced by the local parsers in
[`parse.c`](../src/parse.c) and covered by
[`color_style_test.c`](../tests/color_style_test.c).

#### Interaction with other settings

Double borders work with the existing `style`, `order`, `hidpi`, per-window
`apply-to`, background and blur settings. They support all focus animations:

- `fade` applies to the complete double appearance.
- `slide` uses the complete double-border extent for its endpoints.
- `pulse` scales both stroke widths together while keeping the configured gap
  fixed.
- `ramp` independently animates the glow of every layer configured with
  `glow(...)`; non-glowing layers are unchanged.

Rounded double layers normally expand their radii as they move outward, which
keeps their curves concentric with the detected window radius. The adaptive
radius-0/1 behavior is preserved: when `style=round` detects either value, the
base radius and both expanded layer radii remain `0`, producing completely
square corners. Explicit `style=square` also keeps both layers square.

Changing a double color redraws the matching focused or unfocused borders.
Changing `width` or `double_gap` redraws all borders. The options can also be
included after `apply-to=<window-id>` to create a per-window double-border
override.

### Combined glow and gradient colors

The `feature/glow-gradient` branch makes glow a composable wrapper around the
two existing diagonal gradient forms:

```sh
borders \
  'active_color=glow(gradient(top_left=0xffff0000,bottom_right=0xff0000ff))' \
  'inactive_color=glow(gradient(top_right=0xff00ff00,bottom_left=0xff303030))'
```

Both forms are accepted for `active_color` and `inactive_color`:

```text
glow(gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB))
glow(gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB))
```

The visible stroke retains the requested gradient. Its glow color is computed
from an alpha-weighted blend of both endpoints, rather than simply borrowing
one endpoint. Rendering supports round, square, and uniform border geometry.
The parser requires the complete expression and rejects trailing text without
replacing the previous color; see [`parse.c`](../src/parse.c),
[`border.c`](../src/border.c), and
[`color_style_test.c`](../tests/color_style_test.c).

This nesting is border-only. Background colors accept one solid color and
reject gradients and glow wrappers. The focus-animation `ramp` mode can be
combined with a glow-gradient and animates the strength of its glow.

### Focus-change animations

The `feature/border-animation` branch adds four composable modes:

```sh
borders \
  animation=fade,slide \
  animation_duration=0.35 \
  animation_easing=ease_out_expo
```

| Mode | Effect on the newly focused border |
| --- | --- |
| `fade` | Fades its opacity from transparent to fully visible. |
| `ramp` | Grows a glow from zero to full strength; it has a visible effect only with a `glow(...)` active color. |
| `slide` | Moves the active border from the previous focused-window geometry to the new window. |
| `pulse` | Temporarily expands the stroke, with a larger relative expansion for thin borders. |
| `none` | Disables animations and must be specified by itself. |

Modes are comma-separated with no spaces and run simultaneously. The old
border changes to its inactive appearance immediately; only the newly focused
border animates. A focus change made while the primary mouse button is held is
applied immediately without animation.

`animation_duration=<seconds>` is shared by all selected modes. It defaults to
`0.25` and must be finite and greater than zero. `animation_easing` defaults to
`linear` and accepts exactly:

- `linear`
- `ease_in_expo`
- `ease_out_expo`
- `ease_in_out_expo`

Easing changes slide motion only; fade, ramp, and pulse retain their own fixed
progress curves. `animation=none` cannot be combined with another mode, and an
invalid animation, duration, or easing leaves the previous valid setting in
place. The implementation and parsing live in
[`animation.c`](../src/animation.c), [`border.c`](../src/border.c), and
[`parse.c`](../src/parse.c), with focused tests under
[`tests/`](../tests/).

### Focus-aware window backgrounds and blur

The `codex/background-blur` branch adds a fill and compositor blur behind the
target window. Global values act as fallbacks, while focused and unfocused
overrides can replace them independently:

```sh
borders order=above \
  background_color=0x40000000 \
  blur_radius=20 \
  active_background_color=0x00000000 \
  active_blur_radius=0 \
  inactive_background_color=0x80000000 \
  inactive_blur_radius=20
```

| Option | Meaning | Default |
| --- | --- | --- |
| `background_color=<color>` | Global solid fill fallback | `0x00000000` |
| `active_background_color=<color>` | Focused-window fill override | Not set |
| `inactive_background_color=<color>` | Unfocused-window fill override | Not set |
| `blur_radius=<float>` | Global blur fallback | `0` |
| `active_blur_radius=<float>` | Focused-window blur override | Not set |
| `inactive_blur_radius=<float>` | Unfocused-window blur override | Not set |
| `background_host=auto\|border\|companion` | Where the fill and blur live (see below) | `auto` |
| `renderer=layer\|cg` | Preferred border renderer; `layer` auto-falls back to CG while any blur is active | `layer` |
| `inactive_foreground=on\|off` | With a companion, order the unfocused fill above its target | `off` |

Only a single solid `0xAARRGGBB` value is accepted for a background. Blur
values must be finite and non-negative; values above `50` are capped at `50`,
and `0` disables blur. Blur uses the private
`SLSSetWindowBackgroundBlurRadius` compositor API and may have a performance
cost.

A background counts as visible only when its alpha is greater than zero. When
the resolved color for the current focus state is fully transparent
(alpha `== 0`), the host is `NONE`: no fill window is created and no blur is
applied, even if a blur radius is set. This avoids the previous failure mode
where invisible-plus-blur left a transparent frosted surface — an oversized
grey halo with `order=below`, or a target-sized white box with `order=above`.
For blur without a visible dim, use a near-transparent alpha such as
`0x01000000` (blur-only frost); for a dimmed frost, use e.g. `0x80000000`.

`renderer=layer` is automatically disabled while any blur radius is greater
than zero (`blur_radius`, `active_blur_radius`, or `inactive_blur_radius`).
The border falls back to the CG path so blur and fill stay attached; clearing
all blur radii re-enables the layer path on the next recreation.

Host selection adapts to border order, unless overridden:

- `auto` (default): `order=below` reuses the existing border window for fill
  and blur; `order=above` uses a separate companion window placed below the
  target, keeping fill and blur behind the application while the border stays
  above.
- `border`: force fill and blur onto the border window itself.
- `companion`: force the separate companion window, even with `order=below`.

The companion is target-sized (it covers the application window, not the
oversized border frame). With `order=above` plus forced `companion`, the
`inactive_foreground` flag selects its stacking: `off` (default) orders the
companion `BELOW` the target, `on` orders it `ABOVE` the target. New borders
initialize their focus state before the first draw so the correct host is
picked immediately, and focus gain tears down a dropped inactive companion
synchronously so no dim lingers as a stuck white box.

The companion follows window moves, Spaces, sticky-window state, and yabai
proxy transitions. When its target is hidden, minimized, or no longer on a
visible Space, it is ordered out immediately. Its backing resources are
released after a ten-second grace period unless the target becomes visible
again. The fill mask uses the detected inner corner radius so it covers the
same corner region consistently. Each redraw fully clears the border surface
with `ClearRect` before repainting; there is no partial ring clear and no
retained `fresh_surface` / `interior_painted` state. See [`border.h`](../src/border.h),
[`border.c`](../src/border.c), and
[`PR_BACKGROUND_BLUR.md`](../.github/PR_BACKGROUND_BLUR.md).

All nine settings can be changed at runtime or included in an
`apply-to=<window-id>` override. Supplying a global value later replaces that
same property in existing per-window settings, while unmentioned focused-state
overrides remain in place.

### Adaptive square borders for radius-0/1 windows

The `codex/adaptive-square-radius` branch changes automatic radius mapping; it
does not add a command-line option. On macOS 26, the system preference used to
request nearly square native windows is:

```sh
defaults write -g NSConvolutionOverride1 -float 1
```

After changing it, existing application windows may need to be recreated and
Borders restarted before their geometry is detected again. To remove the
global override:

```sh
defaults delete -g NSConvolutionOverride1
```

With adaptive `style=round`, the local mapping is:

| Radius reported by macOS | Visible outer radius | Inner occlusion-mask radius |
| --- | --- | --- |
| `0` or `1` | `0` | `3.27` |
| Greater than `1` | Reported radius unchanged | Reported radius plus `1` |
| Negative or unavailable | Default `9` | `10` |

Thus nearly square windows receive a genuinely square exterior while windows
reporting normal radii such as `12` or `17` continue matching their native
rounding. The small rounded inner mask paints only the few corner pixels needed
to cover the compositor or shadow wedge; it does not move the border inward or
thicken its straight edges. The same inner radius is used by the local
background mask.

The mapping helper is in [`border.h`](../src/border.h), is applied from
[`windows.c`](../src/windows.c), and is tested by
[`border_radius_test.c`](../tests/border_radius_test.c). The implementation
notes and live-test matrix are in
[`PR_ADAPTIVE_SQUARE_BORDERS.md`](../.github/PR_ADAPTIVE_SQUARE_BORDERS.md).

## `order=below` and `order=above`

### Syntax

```sh
borders order=below
borders order=above
```

`below` is the default. `above` orders the border window above its target
application window; `below` orders it behind the target. Felix describes this
as an "unsupported and undocumented" option in the upstream yabai-integration
discussion, including both supported spellings and the default
([issue #62](https://github.com/FelixKratz/JankyBorders/issues/62#issuecomment-1973516946)).
The default is also initialized as `BORDER_ORDER_BELOW` in
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L31-L46).

The parser actually examines only the first character after `order=`: `a`
selects above and every other successfully parsed character selects below
([`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L102-L109)).
Use the full `above` and `below` values; the looser behavior is an
implementation detail, not a reliable alias.

### What it changes

The selected value is passed directly to the private SkyLight transaction that
orders each border relative to its target window
([`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L223-L243)).
A runtime invocation sets the all-borders update mask, so existing borders are
redrawn and reordered without being destroyed
([`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L105-L109),
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L119-L127)).

Practical trade-off:

- `order=below` cannot cover pixels inside the application window, but the
  border is less likely to overlay window content.
- `order=above` can cover the application edge and is required by upstream's
  special square-corner overlap path, but it is an overlay above the
  application. The square overlap path is conditional on style, order, and
  width in [`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L82-L116).
  Its compile-time minimum is `52` points when built with the macOS 26 SDK and
  `8` points with older SDKs
  ([`border.h`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.h#L9-L21)).

### Important upstream limitations

- Upstream `background_color` is drawn only when the border is **not** above
  the application. With `order=above`, upstream deliberately skips that fill
  ([`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L143-L153)).
  The local background-blur branch adds a separate target-sized companion for
  this case (forced with `background_host=companion`; see above for the
  `inactive_foreground` stacking rule), but that is not upstream behavior.
- Ordering uses private SkyLight APIs. The project already depends on
  SkyLight, but this particular control is explicitly unsupported by the
  maintainer; compositor behavior can change between macOS releases. The
  original experimental notes also warn about apparent width, transparency,
  and flicker caused by overlapping an above-order border with the application
  ([issue #37](https://github.com/FelixKratz/JankyBorders/issues/37#issuecomment-1871262622)).

## `style=uniform`

### Syntax

```sh
borders style=uniform
```

The upstream man page lists `uniform` only in the `<style>` nomenclature; the
option description still says that style is either `round` or `square`. The
missing behavior is that `uniform` forces the **outer** corner radius to `9`
points for every window instead of using each window's detected radius. It
also fills the rounded path before stroking it to cover the difference between
the application's native rounding and the uniform border
([`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L117-L139)).
The feature's upstream issue gives the same command and describes filling the
gap to make window radii visually consistent
([issue #169](https://github.com/FelixKratz/JankyBorders/issues/169)).

Like the other style values, parsing uses the first character, so `uniform`
selects the internal `u` style. Use the complete name in configuration. A
runtime style update redraws all borders; it does not recreate their backing
windows
([`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L102-L112),
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L119-L127)).

This is different from `style=round`: `round` follows the radius detected for
each window on macOS 26, while `uniform` deliberately makes all outer radii
`9`. It is also different from `style=square`, which uses square geometry.

## `apply-to=<window-id>`

### Syntax

```sh
borders apply-to=12345 style=square width=6 \
  active_color=0xffff8800 inactive_color=0xff555555
```

This modifies only the border whose target window has the positive integer ID
`12345`. Felix introduced the syntax on upstream `main` and states that the
other options in the same invocation are applied only to that window
([issue #57](https://github.com/FelixKratz/JankyBorders/issues/57#issuecomment-2002614446)).
The parser accepts a signed decimal integer, while dispatch requires the
resulting ID to be greater than zero
([`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L121-L131),
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L70-L88)).

Use it as a **runtime command against an already-running `borders` process**.
The target must already have a border in JankyBorders' live window table; a
missing ID is silently ignored. The routing code looks up that existing border,
stores a full settings override on it, redraws it, and returns without changing
the global settings
([`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L80-L90)).

### Getting a window ID

Window IDs are macOS system-level identifiers, not yabai-specific identifiers.
The maintainer deliberately leaves mapping application metadata and window
events to a window manager, and recommends persisting per-window behavior in
that window manager's event scripts
([issue #57](https://github.com/FelixKratz/JankyBorders/issues/57#issuecomment-3732902721)).

For the focused yabai window, examples from the upstream issue include:

```sh
wid="$(yabai -m query --windows id --window | jq -r '.id')"
borders apply-to="$wid" style=square
```

AeroSpace users can obtain IDs from `aerospace list-windows --all` or
`aerospace debug-windows`; the relevant value is `Aero.axWindowId`
([upstream issue discussion](https://github.com/FelixKratz/JankyBorders/issues/57#issuecomment-3739498417)).

### Which settings make sense per window

The per-window override is selected by `border_get_settings()` for drawing,
geometry, and ordering
([`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L8-L15)).
Consequently, the useful upstream per-window controls are:

- `style`
- `width`
- `active_color`
- `inactive_color`
- `background_color` (subject to the upstream `order=below` limitation)
- `order`

Do not rely on `apply-to` to make global-selection controls per-window:

- `blacklist` and `whitelist` are consulted through the global settings while
  a target is admitted to the border table
  ([`windows.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/windows.c#L14-L43)).
- `ax_focus` is read globally by focused-window discovery
  ([`windows.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/windows.c#L240-L253)).
- `hidpi` is used when the border backing window is created, while the
  `apply-to` path only updates the existing border
  ([`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L176-L203),
  [`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L80-L88)).

### Override lifetime and later global changes

The override is attached to the border object, so it naturally disappears
when that window's border is destroyed. There is no `reset=<window-id>` option
in the current parser.

Later global invocations are also parsed into every active per-window override.
That means a globally mentioned property replaces the same property in an
override, while unmentioned overridden properties remain specific to the
window. The propagation loop is visible in
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L89-L117).
Options that request full recreation (`hidpi`, `blacklist`, or `whitelist`)
destroy and rebuild every border, which also discards the attached overrides
([`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L90-L100),
[`windows.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/windows.c#L94-L112)).

## Undocumented informational flags

```sh
borders --version
borders -v
borders --help
borders -h
```

`--version` and `-v` print a string such as `borders-v1.9.0` and exit.
`--help` and `-h` do not print an option list; they print `Refer to the man page
for help: man borders` and exit. These are recognized only when they are the
first argument
([`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L14-L22),
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L171-L182)).

## Implemented behavior with no configuration knob

These are useful to know when diagnosing behavior, but they cannot be toggled
with a `borders` argument.

### Border-window shadows are always disabled

Every border backing window gets `com.apple.WindowShadowDensity = 0` when it is
created
([`window.h`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/misc/window.h#L228-L288)).
Felix explicitly chose not to provide an option to re-enable the extra shadow
([issue #23](https://github.com/FelixKratz/JankyBorders/issues/23#issuecomment-1825313832)).

### yabai animation proxies are tracked automatically

The upstream build enables its yabai integration in the source and registers
the `git.felix.jbevent` Mach port at startup
([`yabai.h`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/misc/yabai.h#L1-L10),
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L227-L235)).
With a compatible yabai version, proxy begin/end events cause JankyBorders to
swap to a proxy border and track the animated window transform, then restore
the real border when the animation ends
([`yabai.h`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/misc/yabai.h#L58-L190)).
There is no JankyBorders CLI setting for this integration.

### macOS 26 window radii are detected per window

On macOS 26, upstream dynamically resolves the private
`SLSWindowIteratorGetCornerRadii` symbol
([`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L24-L25),
[`main.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/main.c#L162-L169)).
When a border is created, the first returned radius is used; a missing, zero,
or negative result falls back to `9`, and the inner mask uses radius plus one
([`windows.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/windows.c#L64-L81)).
This is automatic and primarily affects documented `style=round`. The origin
and intent of the private API are recorded by the maintainer in
[issue #160](https://github.com/FelixKratz/JankyBorders/issues/160#issuecomment-3282409924).

Because detection happens during border creation, changing external macOS
window geometry preferences may require recreating the application window or
restarting Borders before the stored radius changes. The local
`codex/adaptive-square-radius` branch changes the handling of valid radius
`0`/`1`; that branch delta is already documented in
[`../.github/PR_ADAPTIVE_SQUARE_BORDERS.md`](../.github/PR_ADAPTIVE_SQUARE_BORDERS.md)
and is therefore not duplicated here.

### Sticky windows carry their border across Spaces

During an update, JankyBorders reads the target's private sticky-window tag and
marks the border sticky as well. The border then receives the matching sticky
tag instead of being treated as belonging only to its original Space
([`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L176-L185),
[`border.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.c#L246-L255)).
This automatic behavior was added to fix yabai sticky windows whose borders
did not follow them between Spaces
([issue #67](https://github.com/FelixKratz/JankyBorders/issues/67#issuecomment-1939285193)).
There is no `sticky=` option in JankyBorders; the window manager controls the
target window's state.

### Transient windows tagged "ignores cycle" are excluded

Upstream filters out windows carrying the private ignore-cycle tag when it
decides whether a window is suitable for a border
([`window.h`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/misc/window.h#L6-L25)).
This prevents borders on tooltips and similar transient UI, including JetBrains
tooltips; it was merged in
[PR #178](https://github.com/FelixKratz/JankyBorders/pull/178).
There is no opt-out.

## Things that look configurable in history or source but are not upstream options

### `blur_radius` was removed from upstream

Background blur was merged in
[PR #60](https://github.com/FelixKratz/JankyBorders/pull/60), then removed after
reproducing excessive WindowServer GPU use. The maintainer's removal note says
a more robust implementation would likely require a CALayer-backed approach
([issue #78](https://github.com/FelixKratz/JankyBorders/issues/78#issuecomment-2003758120)).
Although the current upstream settings struct still contains a `blur_radius`
field initialized to zero, the upstream parser has no `blur_radius=` case and
the value is not used by the renderer
([`border.h`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/border.h#L31-L52),
[`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L61-L136)).

The local `codex/background-blur` branch implements a different, focus-aware
version and documents it in the local README, man page, and
[`../.github/PR_BACKGROUND_BLUR.md`](../.github/PR_BACKGROUND_BLUR.md). It is
not an upstream hidden option.

### There is no shadow, radius, or per-window-reset argument

- Shadow suppression is fixed at creation time; there is no shadow parser case.
- Radius selection is automatic; there is no `radius=` parser case.
- A per-window override has no explicit reset command. It ends with the border
  object's lifetime or a recreate-all update.

These absences are confirmed by the complete upstream settings parser
([`parse.c`](https://github.com/FelixKratz/JankyBorders/blob/a7297ca7d1933f3a30b12e8f10750e8d84eeee1e/src/parse.c#L61-L136)).

## Example configs (copy-paste)

All commands update a running `borders` process. Colors are `0xAARRGGBB`.
Arguments with parentheses need shell quoting.

### 1) Minimal outline only

No fill, no blur; just a focus-colored stroke.

```sh
borders \
  style=round \
  width=5.0 \
  active_color=0xffe2e2e3 \
  inactive_color=0xff414550
```

### 2) Active/inactive colors

High-contrast focus distinction; same geometry as above.

```sh
borders \
  style=round \
  width=6.0 \
  active_color=0xffff8800 \
  inactive_color=0xff555555
```

### 3) Blur-only inactive

Near-transparent fill (`0x01000000`) keeps the blur visible without dimming.
A fully transparent (`0x00000000`) fill disables blur for that state.

```sh
borders order=above \
  active_background_color=0x00000000 \
  active_blur_radius=0 \
  inactive_background_color=0x01000000 \
  inactive_blur_radius=20
```

### 4) Monocle-like dim + blur inactive

Dims unfocused windows above their content. Requires `order=above`; the
companion stays below the target by default, `inactive_foreground=on` puts the
inactive fill above it.

```sh
borders order=above inactive_foreground=on \
  active_background_color=0x00000000 \
  active_blur_radius=0 \
  inactive_background_color=0x80000000 \
  inactive_blur_radius=20
```

### 5) Below vs above order

```sh
# Default: border behind the window, never covers app content
borders order=below style=round width=5.0 active_color=0xffe2e2e3

# Overlay: border above the window, covers the edge;
# required for the square-corner overlap path and dim-above
borders order=above style=round width=5.0 active_color=0xffe2e2e3
```

### 6) Background_host border vs companion

`auto` (default) uses the border window with `order=below` and a target-sized
companion with `order=above`.

```sh
# Force fill and blur onto the border window itself;
# with order=above they composite above the application
borders order=below background_host=border \
  background_color=0x40000000 blur_radius=20

# Force the separate target-sized companion even with order=below
borders order=below background_host=companion \
  background_color=0x40000000 blur_radius=20
```

### 7) Renderer layer vs cg

`layer` (default) is CoreAnimation with no backing store. Any blur radius
above `0` auto-falls back to the CG path; clearing all blur radii re-enables
the layer path on the next recreation. Either value recreates all borders.

```sh
# Default: CoreAnimation (auto-falls back to CG while blur is active)
borders renderer=layer style=round width=5.0 active_color=0xffe2e2e3

# Force CoreGraphics
borders renderer=cg style=round width=5.0 active_color=0xffe2e2e3
```
