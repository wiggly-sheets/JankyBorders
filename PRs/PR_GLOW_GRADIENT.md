# feat: support glow with gradient borders

## Summary

Adds support for combining the existing `glow(...)` and `gradient(...)`
color styles.

Gradient borders can now be configured with either supported direction:

```sh
borders \
  'active_color=glow(gradient(top_left=0xff9ece6a,bottom_right=0xfff7768e))'
```

```sh
borders \
  'active_color=glow(gradient(top_right=0xff9ece6a,bottom_left=0xfff7768e))'
```

Existing solid colors, solid glows, and non-glowing gradients remain
compatible.

## Motivation

Previously, glow and gradient were mutually exclusive color styles. Users
could configure either:

```sh
active_color='glow(0xff9ece6a)'
```

or:

```sh
active_color='gradient(top_left=0xff9ece6a,bottom_right=0xfff7768e)'
```

but could not combine them.

This change makes glow an independent property of a color style, allowing it
to compose with both solid colors and gradients without duplicating color
style variants.

## Implementation

### Color representation

`struct color_style` now separates the underlying color representation from
the glow property:

- `stype` determines whether the color is solid or a gradient.
- `glow` determines whether glow rendering is enabled.

This avoids adding separate enum cases for every possible combination, such
as solid glow and gradient glow, and makes future style composition easier.

### Parsing

Color parsing is split into helpers for solid colors and gradients.

The parser:

- Supports both gradient directions inside `glow(...)`.
- Requires the entire argument to match the expected syntax.
- Parses into temporary values before modifying the destination style.
- Leaves the existing configuration unchanged when parsing fails.
- Rejects gradients for `background_color`, which currently supports only
  solid colors.

Requiring complete input consumption also prevents malformed values such as
the following from being partially accepted:

```sh
active_color='glow(0xff9ece6a)trailing'
```

### Rendering

Gradient glow rendering is handled by a shared helper used for both square
and rounded borders.

CoreGraphics shadows accept a single shadow color rather than a spatial
gradient. The glow color is therefore calculated as an alpha-weighted blend
of the gradient's two endpoint colors. The border itself remains a true
gradient.

The helper:

1. Creates the glow using the blended endpoint color.
2. Draws the appropriate square or rounded border path.
3. Removes the solid center using `kCGBlendModeDestinationOut`.
4. Draws the original gradient border over the glow.

The existing reduced blur behavior for thick square borders ordered above
their target window is preserved for gradient glows. Existing solid glow
rendering is unchanged.

A true per-pixel gradient blur is intentionally outside this PR. That would
require either an offscreen blurred image or a multi-pass gradient
approximation and should be evaluated separately for rendering and animation
performance.

## Supported values

The complete set of color forms remains:

```text
0xAARRGGBB
glow(0xAARRGGBB)
gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB)
gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB)
glow(gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB))
glow(gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB))
```

Arguments containing parentheses generally need to be quoted by the shell.

## Tests

Added a focused color-style regression test covering:

- Both glow-gradient directions.
- Existing plain gradients.
- Existing solid glows.
- Existing solid colors.
- Rejection of trailing invalid input.
- Preservation of the previous style after invalid input.
- Alpha-weighted endpoint color blending.

Validated with:

```sh
make test
make debug
make
```

A sanitizer-enabled build also compiles successfully.

## Documentation

Updated both the scdoc source and generated man page with:

- The two new supported configuration forms.
- Shell quoting guidance.
- The endpoint-blending behavior used for gradient glow colors.

## Compatibility

This is backward compatible with existing configuration syntax. No existing
option is removed or renamed.

The only internal representation change is replacing a dedicated glow style
variant with a composable `glow` flag.

## Visual verification

Tested with:

- [ ] Rounded borders
- [ ] Uniform rounded borders
- [ ] Square borders
- [ ] Borders ordered above windows
- [ ] Borders ordered below windows
- [ ] Partially transparent gradient endpoints
- [ ] Active and inactive gradient-glow configurations

<!-- Add before/after screenshots or a short recording here. -->
