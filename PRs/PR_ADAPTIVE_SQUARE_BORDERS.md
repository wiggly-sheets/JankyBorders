# fix: make adaptive borders square for radius-0/1 windows

**Base:** `codex/background-blur`  
**Head:** `codex/adaptive-square-radius`

## Summary

Make `style=round` produce a genuinely square exterior for windows that macOS
reports as having a corner radius of `0` or `1`, while preserving the detected
geometry of normally rounded windows.

This allows the adaptive round style to follow both window classes:

- Nearly-square windows receive a square outer border.
- Rounded windows continue receiving borders that match their detected radius.

## Motivation

On macOS 26, a window can be made nearly square with the global convolution
override:

```sh
defaults write -g NSConvolutionOverride1 -float 1
```

The macOS per-window corner-radius API can report a radius of `1` for windows
using this geometry. Previously, `style=round` passed that radius directly to
the border path:

```c
border->radius = detected_radius;
border->inner_radius = detected_radius + 1;
```

A reported radius of `1` therefore still produced a subtly rounded exterior.

Using `style=square` is not an ideal substitute because it forces every window
into the square rendering path, including applications with large native corner
radii. This change keeps the normal adaptive path and applies the square
treatment only where the radius API identifies an affected window.

After changing `NSConvolutionOverride1`, existing application windows may need
to be recreated, and Borders may need to be restarted, before their stored
geometry reflects the new setting.

## Implementation

Detected radii are now mapped as follows:

| Detected radius | Outer radius | Inner mask radius |
| --- | ---: | ---: |
| `0` | `0` | `3.27` |
| `1` | `0` | `3.27` |
| `12` | `12` | `13` |
| `17` | `17` | `18` |

Conceptually:

```c
bool nearly_square = detected_radius <= 1;

border->radius = nearly_square
                 ? 0
                 : detected_radius;

border->inner_radius = nearly_square
                       ? BORDER_TSMN
                       : detected_radius + 1;
```

The square outer radius makes the visible exterior corner square.

The inner mask deliberately retains the existing `BORDER_TSMN` radius of
`3.27pt`. This permits limited additional painting at the inside corner without
moving the border path inward or changing the thickness of its straight edges.

Windows reporting a radius greater than `1` retain the existing adaptive
behavior unchanged.

## Radius fallback behavior

The legacy default radius of `9` is now named `BORDER_DEFAULT_RADIUS`.

It remains in use when:

- The macOS corner-radius API is unavailable.
- The returned value cannot be converted.
- A negative radius is received.

A genuine API result of `0` is no longer mistaken for an unavailable radius and
replaced with the default.

## Scope

The visible outer-radius change is intended for the adaptive `style=round`
path.

This PR does not:

- Force rounded windows into a square rendering path.
- Move the border inward.
- Increase border width.
- Apply a global inset.
- Change detected radii greater than `1`.
- Attempt to eliminate every compositor-colored pixel that may remain inside
  corners under custom macOS convolution settings.

The goal is specifically to make the exterior border geometry square for the
radius-0/1 window class without degrading normal rounded windows.

## Testing

A focused radius-mapping test was added to the standard `make test` target.

It verifies:

- Radius `0` produces outer radius `0` and inner radius `3.27`.
- Radius `1` produces outer radius `0` and inner radius `3.27`.
- Radius `12` remains outer radius `12` with inner radius `13`.
- Radius `17` remains outer radius `17` with inner radius `18`.
- Invalid negative input falls back to outer radius `9` and inner radius `10`.

Validation performed:

- `make test`
- `make`
- Live comparison with macOS reporting radius `1` for nearly-square windows.
