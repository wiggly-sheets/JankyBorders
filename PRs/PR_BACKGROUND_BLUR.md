# feat: add focus-aware window backgrounds and blur

**Base:** `feature/border-animation`  
**Head:** `codex/background-blur`

## Summary

Add configurable background fill and blur layers behind bordered windows,
including independent focused and unfocused overrides.

The implementation supports both border ordering modes:

- With `order=below`, the existing border window hosts the background and blur.
- With `order=above`, a companion window is created beneath the application
  window so the visible border can remain above it.

## Configuration

This adds the following options:

- `background_color=<color>`
- `active_background_color=<color>`
- `inactive_background_color=<color>`
- `blur_radius=<float>`
- `active_blur_radius=<float>`
- `inactive_blur_radius=<float>`

The global values act as fallbacks. Active and inactive overrides apply only
when explicitly configured.

Example applying fill and blur to every window:

```sh
borders \
  order=above \
  background_color=0x80000000 \
  blur_radius=20
```

Example applying them only to inactive windows:

```sh
borders \
  order=above \
  inactive_background_color=0x80000000 \
  inactive_blur_radius=20
```

Blur radius accepts finite, non-negative values. Values above `50` are capped
at `50`, and `0` disables blur.

Background colors remain limited to solid `0xAARRGGBB` values; gradients are
rejected.

## Rendering behavior

Background hosting is selected dynamically from the effective focused-state
settings:

- No visible fill and no blur: no background host is needed.
- `order=below`: reuse the existing border window.
- `order=above`: create a companion window beneath the target application
  window.

For above-order borders, the companion window:

- Tracks the target window frame and Space.
- Uses the same detected inner corner radius as the border mask.
- Receives matching window level, sublevel, tags, and transforms.
- Moves transactionally with its border and target.
- Remains below the application while the border remains above it.
- Is hidden immediately when the target is hidden, minimized, or leaves the
  visible Space.

The companion backing window is retained for a ten-second grace period after
being hidden. This avoids repeatedly destroying and recreating resources during
brief visibility changes. The pending eviction is cancelled if the target
becomes visible again.

Blur is applied through the private `SLSSetWindowBackgroundBlurRadius` API and
remains disabled by default.

## Focus and runtime updates

Active and inactive settings use focused update masks so changing one state
only redraws the affected borders.

Switching between focus states updates:

- Effective background color.
- Effective blur radius.
- Whether a background host is needed.
- Companion placement and visibility.

Yabai proxy transitions now hide and restore companion background windows
together with their corresponding border windows.

## Lifecycle and synchronization

Background windows and drawing contexts are released with their owning borders.

The border lifecycle was hardened to support the additional companion
resources:

- Border destruction is idempotent.
- Destroyed borders reject later move, update, and unhide operations.
- Drawing contexts are recreated after window shape changes.
- Border and companion movement is synchronized under the border mutex.
- Updates remain on the synchronized path so the border and companion cannot
  diverge during concurrent changes.
- Mutex attributes and mutexes are released during teardown.

Hidden companion resources are evicted safely by resolving the target window
through the live border table rather than retaining an unsafe raw pointer.

A periodic cleanup pass also removes orphaned borders whose target windows are
no longer valid.

## Additional robustness

Supporting paths touched by this work now handle several failure cases
explicitly:

- Window-spawn event payloads are length-checked and copied before use.
- Core Foundation number arrays handle empty input and allocation failures.
- Gradient allocation failures fall back to a solid color instead of drawing
  with a null gradient.
- Off-Space borders are actively hidden during consistency checks.

## Documentation

The README and man page now document:

- Background and blur configuration.
- Active and inactive overrides.
- Above-order companion behavior.
- Below-order window reuse.
- Blur limits and private-API considerations.
- The ten-second hidden-resource grace period.

## Testing

- `make test`
- `make`

The parser tests cover:

- Global background fallback.
- Active and inactive overrides.
- Focus-specific update masks.
- Above-order and below-order host selection.
- Blur parsing and maximum-value clamping.
- Invalid negative blur values.
- Disabling the background host when both fill and blur are absent.
- Companion placement decisions during animation.
