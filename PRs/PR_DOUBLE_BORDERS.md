# feat: add independently styled double borders

**Base:** `codex/adaptive-square-radius`  
**Head:** `codex/double-borders`

## Summary

Add an optional double-border appearance whose outer and inner layers can be
configured independently for focused and unfocused windows.

Each layer supports the complete existing border-color vocabulary: solid,
glow, gradient, and glow-gradient. Width and transparent spacing are also
configurable per double appearance.

```sh
borders \
  'active_color=double(glow(gradient(top_left=0xffff2d55,bottom_right=0xffffcc00)),glow(0xffffffff))' \
  'inactive_color=double(gradient(top_right=0xff2c2c2e,bottom_left=0xff636366),0xff8e8e93)' \
  'width=double(4.0,2.0)' \
  double_gap=1.0
```

The first nested value is the outer layer and the second is the inner layer.
Existing single-border configurations continue to work unchanged.

## Configuration

Double appearances use the existing focused-state settings:

- `active_color=double(<outer-style>,<inner-style>)`
- `inactive_color=double(<outer-style>,<inner-style>)`

Each nested style can independently be one of:

```text
0xAARRGGBB
glow(0xAARRGGBB)
gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB)
gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB)
glow(gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB))
glow(gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB))
```

This gives four independently styled positions: active outer, active inner,
inactive outer, and inactive inner.

Styles are applied inside `double(...)`. Wrapping the complete expression, as
in `glow(double(...))`, is intentionally unsupported because per-layer
wrapping is more expressive and avoids a second set of composition rules.

Widths use the same outer/inner order:

- `width=double(<outer-width>,<inner-width>)` configures them independently.
- A scalar `width=<float>` applies the same width to both layers.
- When the selected color is a single style, only the outer width is rendered.

The new `double_gap=<float>` option adds transparent spacing between the
layers. It defaults to `0` and accepts finite, non-negative values.

## Parsing and settings model

Border colors are represented as an appearance containing one or two ordinary
color styles. This keeps solid, glow, gradient, and glow-gradient behavior in
one implementation instead of duplicating the style model for double borders.

The double parser finds the top-level separator while ignoring commas nested
inside gradient expressions. It parses both sides independently and replaces
the previous appearance only when the entire expression is valid.

Invalid widths, malformed expressions, nested double expressions, trailing
text, and `glow(double(...))` are rejected without partially updating the
previous setting. Double expressions are also rejected for background colors,
which remain single-color settings.

## Rendering

The renderer draws two independently styled concentric strokes outside the
application window:

1. The inner stroke begins at the normal window edge.
2. The optional transparent gap follows it.
3. The outer stroke surrounds both.

Solid, glow, gradient, and glow-gradient rendering is selected separately for
each layer. A glowing outer layer does not leak shadow state into a non-glowing
inner layer.

The border backing window reserves the larger of the focused and unfocused
appearance extents. This prevents clipping or backing-window resizing when
focus switches between single and double appearances.

Rounded layers increase their radius with their outward offset so they remain
concentric with normally rounded windows. A base radius of `0` remains `0` for
both expanded layers, preserving completely square double borders when the
adaptive radius mapping receives `0` or `1`. Explicit square style also uses
square paths for both layers.

## Animation behavior

Double borders integrate with every focus-animation mode:

- `fade` changes the opacity of the complete appearance.
- `slide` calculates endpoints using the full double-border extent.
- `pulse` scales both widths proportionally while preserving the configured
  gap.
- `ramp` animates each glowing layer independently and leaves non-glowing
  layers unchanged.

## Documentation and tests

The README and man page document the nested style syntax, independent widths,
gap, animation behavior, and adaptive square-corner behavior.

Parser and geometry tests cover:

- Single-style backward compatibility.
- Active and inactive double appearances.
- Solid, glow, gradient, and glow-gradient nested styles.
- Both gradient directions.
- Independent and scalar widths.
- Gap and total-extent calculation.
- Invalid expression rollback.
- Rejection of whole-double wrappers.
- Zero-radius double-layer squaring.
- Concentric radius expansion for normally rounded windows.

Validation performed:

- `make test`
- `make`
- `git diff --check`
- Warning-enabled compilation with no new warnings from this feature

Live desktop rendering was not started during automated validation because
launching the binary can update an already-running Borders instance.
