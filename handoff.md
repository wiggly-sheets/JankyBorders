# Handoff — Integrating PR #208 (CAContext layer rendering) into main

Single source of truth for whoever picks up this in-progress merge. This document describes the exact conflict state, the agreed decisions, and the per-conflict resolution plan. Read this file fully before touching anything.

---

## 1. Current state

**We are MID-MERGE on branch `main`.** `git merge pr-208` was started and is in conflict state. The conflicted working tree is still on disk; the files below contain unresolved conflict markers right now.

**8 files with unresolved conflicts (UU status), 21 conflict blocks total:**

| File | Conflict blocks | Notable content |
|------|----------------|-----------------|
| `src/border.c` | 11 | The hard one. CG feature path (ours) vs layer/clear/nine-slice path (PR) |
| `src/border.h` | 3 | settings fields; static-inline helper block vs forward decl; function declarations |
| `src/windows.c` | 2 | focus/create block; animation helpers vs `windows_window_refresh` |
| `src/events.c` | 1 | yabai reorder handler |
| `src/main.c` | 1 | settings struct initializer |
| `makefile` | 1 | `CC` line + `FILES` source list |
| `src/parse.c` | 1 | blur-radius parsing vs `renderer=` parsing |
| `src/misc/drawing.h` | 1 | nine-slice block (additive, ours empty in that region) |

**New files staged from the PR (A status):** `src/layer.h`, `src/layer.m`.

**Auto-merged cleanly (no markers, verified):** `docs/borders.1`, `docs/borders.1.scd`, `src/misc/extern.h`, `src/misc/helpers.h`, `src/misc/window.h`, `src/windows.h`.

**Git state facts (as briefed):**
- `pr-208` is a local branch fetched via `git fetch origin pull/208/head:pr-208` (upstream FelixKratz/JankyBorders PR #208 by dawidvdh, 2 commits, head `35c5622`, based on `a7297ca` — exactly where our main was before the audit-fixes merge).
- Local `main` is 48 commits ahead of `origin/main` (FelixKratz upstream; no push access). Pushes go to the `wiggly-sheets` remote (the user's fork), NOT `origin`.
- The merge is in progress ON `main`; first step is `git merge --abort`, then branch and redo there (see Step 1).

**Ground truth verified by reading the conflicted files (line numbers refer to the current conflicted working tree — useful anchors for the resolver):**

- `src/border.c` battle map: B1 = lines 85–254, B2 = 301–499, B3 = 634–643, B4 = 800–814, B5 = 946–956, B6 = 964–972, B7 = 988–1010, B8 = 1069–1081, B9 = 1104–1110, B10 = 1183–1193, B11 = 1425–1473. Detailed per-block plan in Step 2.
- `struct border` in `src/border.h` (lines 317–386) has **already auto-merged**: `fresh_surface`, `interior_painted`, `stale_props`, `tags_applied`, `applied_sticky`, `nine_slice`, `use_layer`, `layer`, `layer_failed` are all present *outside* any conflict markers. Do NOT re-add them.
- `src/windows.h` line 12 already declares `windows_window_refresh` (auto-merged).
- `src/misc/extern.h` line 43 already declares `SLSSetWindowLayerContext` (auto-merged; layer.m's dlsym path doesn't strictly need it but the declaration is consistent).
- The constants layer.m needs are present in `src/border.h`: `BORDER_TSMN` (line 18), `BORDER_STYLE_ROUND_UNIFORM` (line 14), `BORDER_STYLE_SQUARE` (line 15).
- `src/events.c` lines 14–73 (the `coalesced_job` struct, `schedule_coalesced()`, `g_focus_job`/`g_space_job`, `schedule_focus_update()`, `schedule_space_update()`) are **already auto-merged** — our side never touched that region. The only remaining `DELAY_ASYNC_EXEC_ON_MAIN_THREAD` reference in the repo is inside the single events.c conflict. Most of the conversion work claimed in the plan is already done by git.
- Our `border_hide` (src/border.c line 1366) and `border_unhide` (line 1415) **already exist** and are richer than PR's (they also order the background/companion window and schedule its eviction). PR's duplicates inside B11 must be dropped.
- `make test` runs **14** test binaries (counted in `makefile` lines 16–30 and `tests/`), not 15. List: border-radius, color-style, animation-focus, animation-tick, animation-parse, windows-regression, shimmer-state, border-geometry, window-space, settings-ownership, parser-bounds, message-payload, config-execution, mach-message.
- **Two merge gaps found during verification that the resolver MUST close (details in Step 2 / Risks):**
  1. `border_create_window` (src/border.c lines 927–937) creates a layer border with **no style-expressibility check** — multi-color, double-layer, shimmer, and animations would silently take the layer path and render wrong (or garbage) output. A gate must be added.
  2. `border_layer_style` (line 891) assigns `settings->active_window` (a `struct border_appearance`) to `style.color` (a `struct color_style`) — a type mismatch that will not compile against our header. It must read `...active_window.layers[0]`.

---

## 2. The two sides being merged

### Our main (HEAD side) — the "audit-fixes" work

Heavy CG-path feature work (all on top of upstream `a7297ca`):

- Background windows with blur: companion windows, `SLSSetWindowBackgroundBlurRadius`, eviction timers (`BACKGROUND_EVICTION_DELAY_SECONDS`).
- Gradient glow borders (`border_draw_gradient_glow`, `border_add_gradient_glow_path`).
- Double-layer borders (`inner_border_width`, `double_border_gap`, `border_draw_double_layer`, `border_double_layer_centers`).
- Multi-color borders (`style->multi` top/left/right/bottom, `border_draw_multi_color`).
- Shimmer (animated color cycling, `SHIMMER_MAX_COLORS`, `border_shimmer_color`).
- Border animations: fade/ramp/slide/pulse, `animation.c`, `border_update_animating`, animation-aware focus (`windows_window_focus_with_mouse_state`, `windows_cancel_border_animation`, `windows_border_frame_offset`, `windows_border_shimmer_enabled`).
- Settings filter tables: blacklist/whitelist with ownership semantics (`settings_*` helpers in border.h).
- `blur_radius=`/`active_blur_radius=`/`inactive_blur_radius=` config options.
- `background_mode=`/`background_host=` logic (`border_background_host`, `border_should_update_background_placement`).
- Settings background overrides (`active_background_override`, `inactive_background_override`, `active_blur_override`, `inactive_blur_override`, `inactive_foreground`).
- 14 test binaries via `make test`.
- A Hz-only frame-line `CC = clang` in the makefile.

### pr-208 side — the "perf/lean-borders" work

Rendering rewrite: borders become type-5 SkyLight windows attached to a `CAContext`; the ring is rasterized once into a small nine-slice template and stretched via `CALayer.contentsCenter`; a resize is just a CATransaction updating layer bounds (no backing store, no rasterization). Key pieces:

- New files `src/layer.h` + `src/layer.m`: `layer_border_create/destroy/update`, `layer_supported()`, `layer_style` struct, runtime `CAContext` resolution via dlopen-style interface declarations (falls back to CG path if unavailable).
- New `renderer=layer|cg` config option; `force_cg` and `show_background` settings fields.
- `border_invalidate_props()`, `windows_window_refresh()`, `schedule_focus_update()`/`schedule_space_update()` — coalesced main-queue timers (`coalesced_job` struct + `schedule_coalesced()`), replacing the old `DELAY_ASYNC_EXEC_ON_MAIN_THREAD` sleeping-thread-per-event approach. PR's `schedule_focus_update` calls `windows_determine_and_focus_active_window(&g_windows)` — the same function our main already has.
- Nine-slice CG fallback in `src/misc/drawing.h` (struct `nine_slice`, `nine_slice_cache`, `drawing_nine_slice_build`, `drawing_draw_nine_slice`, cached corner/band rasterizations).
- `border_clear` ring optimization (clear only the ring strips, not the full frame), `fresh_surface`/`interior_painted`/`stale_props`/`use_layer` border fields.
- CG path kept as fallback (auto when CAContext unavailable, or forced via `renderer=cg`).

---

## 3. Decisions already made (do not revisit)

1. **Approach:** Merge `pr-208` and resolve conflicts keeping BOTH sides. Do NOT reimplement the layer approach from scratch.
2. **Feature fallback:** multi-color, double-layer, and shimmer borders stay on the CG path (auto-fallback or `renderer=cg`). Solid/gradient/glow get the layer perf win. This is accepted — the layer path cannot express those three features (nine-slice model limitation), and that's fine.
3. **Branch:** Work happens on a new branch `rewrite/layer-rendering` off current local `main` (per user request), NOT directly on main. The merge currently in progress on `main` must be aborted first, then redone on the new branch.

---

## 4. Step-by-step plan

### Step 1: Reset and branch

```sh
git merge --abort          # discard the in-progress merge on main
git status                 # verify clean, on main
git checkout -b rewrite/layer-rendering main
git merge pr-208 --no-edit # reproduces the conflicts
```

Then resolve per Step 2, file by file, using `git add` as each file completes.

### Step 2: Resolve conflicts — per-file strategy (keep both sides)

**Order matters:** resolve `src/border.h` first (everything else depends on it), then `src/misc/drawing.h`, then the smaller files, then `src/border.c`.

#### `src/border.h` (3 conflicts)

- **C1 (settings struct, ~line 79):** keep our fields (`active_background_override`, `inactive_background_override`, `active_blur_override`, `inactive_blur_override`, `inactive_foreground`, `background_mode`) AND add PR's `show_background`, `force_cg`.
- **C2 (~line 122):** keep our entire static-inline helper block (settings_reset_filter_tables, settings_destroy, settings_init_filter_tables, settings_clone_filter_table, settings_clone, settings_snapshot, settings_move, settings_replace, settings_take_filter_ownership, border_window_state enum, settings_shimmer_enabled, border_appearance_extent, border_max_extent, border_effective_order, border_layer_corner_radius, border_background_host enum, border_background_color, border_background_blur_radius, border_background_visible, border_should_update_background_placement) AND add PR's `struct layer_border;` forward declaration.
- **C3 (~line 404):** keep our declarations (`border_space_change_begin`, `border_update_animating`) AND add PR's `border_invalidate_props`.
- Do NOT touch the `struct border` body (lines 317–386) — it already auto-merged with both sides' fields.

#### `src/misc/drawing.h` (1 conflict, lines 158–407)

Take PR's nine-slice block wholesale (our side had nothing there; it is additive). It calls `drawing_set_stroke_and_fill`, `drawing_clip_between_rect_and_path`, `drawing_draw_rounded_rect_with_inset`, `drawing_draw_square_with_inset`, `drawing_nine_slice_release` — all present in our common header. This block MUST land: `struct border.nine_slice` (already in border.h) and `border.c` both reference `nine_slice_cache`.

#### `makefile` (1 conflict, lines 1–6)

Keep our `CC = clang` line; take PR's `FILES` line which adds `src/layer.m` to the source list. Result:

```make
CC = clang
FILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/windows.c src/border.c src/animation.c src/layer.m
```

`LIBS` (line 7) already contains `-framework QuartzCore -framework AppKit`, so layer.m has what it needs.

#### `src/main.c` (1 conflict, lines 49–54)

Keep our `.background_mode = BORDER_BACKGROUND_AUTO,` AND add PR's `.force_cg = false, .show_background = false,`. Result:

```c
.border_style = BORDER_STYLE_ROUND,
.hidpi = false,
.background_mode = BORDER_BACKGROUND_AUTO,
.force_cg = false,
.show_background = false,
.border_order = BORDER_ORDER_BELOW,
```

#### `src/parse.c` (1 conflict, lines 672–704)

Keep our `active_blur_radius=`/`inactive_blur_radius=`/`blur_radius=` parsing AND add PR's `renderer=layer`/`renderer=cg` parsing. PR's `renderer=layer` sets `settings->force_cg = false` and `renderer=cg` sets `settings->force_cg = true`; both OR in `BORDER_UPDATE_MASK_RECREATE_ALL`. The closing `}` after the if/else chain is common — resolve so both if-chains sit before it:

```c
else if (str_starts_with(arguments[i], "blur_radius=")) {
  if (parse_blur_radius(&settings->blur_radius,
                        arguments[i] + strlen("blur_radius="),
                        "blur_radius")) {
    update_mask |= BORDER_UPDATE_MASK_ALL;
  }
}
else if (strcmp(arguments[i], "renderer=layer") == 0) {
  update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
  settings->force_cg = false;
}
else if (strcmp(arguments[i], "renderer=cg") == 0) {
  update_mask |= BORDER_UPDATE_MASK_RECREATE_ALL;
  settings->force_cg = true;
}
```

#### `src/events.c` (1 conflict, lines 144–154)

Keep our explanatory comment about yabai autoraise ordering (the 10000us delay exists because yabai emits reorder before focus state settles) but take PR's calls. Note: the coalesced-jobs infrastructure is already auto-merged (lines 14–73); after this conflict resolves there will be zero `DELAY_ASYNC_EXEC_ON_MAIN_THREAD` references left anywhere:

```c
} else if (event == EVENT_WINDOW_REORDER) {
  debug("Window Reorder (and focus): %d\n", wid);
  // yabai autoraise emits reorder before focus state settles. Updating here
  // orders a border/blur companion with stale focus state and can expose a
  // one-sided blur seam. Let the subsequent focus pass order both surfaces.
  windows_window_refresh(windows, wid);
  schedule_focus_update(10000);
}
```

`windows_window_refresh`, `schedule_focus_update`, `schedule_space_update`, and the coalesced timers are already fully defined outside the conflict — nothing else to do in this file.

#### `src/windows.c` (2 conflicts)

- **C1 (lines 115–119):** keep our `if (g_settings.active_only) border->focused = true;` AND add PR's `border_invalidate_props(border);` — both lines, in that order.
- **C2 (lines 208–325):**
  - Keep our animation-aware functions: `windows_cancel_border_animation`, `windows_border_frame_offset`, `windows_border_shimmer_enabled`, `windows_window_focus_with_mouse_state` (lines 209–313). They are referenced by the common code below (the mouse-state wrapper at line 361 and the shimmer checks at 121/350/356).
  - Take PR's `windows_window_refresh` (lines 316–322).
  - Drop PR's `static bool windows_window_focus(struct table* windows, uint32_t wid) {` header (line 324). The common region (lines 326–359) is the focus-loop body, and our real `windows_window_focus` wrapper lives at lines 361–365 (mouse-state aware). Keeping PR's header would produce a duplicate definition.
  - Post-resolution check: exactly one `windows_window_focus` (the wrapper), one `windows_window_refresh`, and `windows_border_shimmer_enabled` still defined. Note the common region already calls `border_invalidate_props` (lines 337, 345) — another reason B11 must land.

#### `src/border.c` (11 conflicts — the hard one)

Integrate PR's layer path alongside our CG features. Per block:

| Block | Lines | Keep ours | Take from PR | Notes |
|-------|-------|-----------|--------------|-------|
| **B1** | 85–254 | Background-window management: `border_destroy_background_window`, `border_cancel_background_eviction`, `border_schedule_background_eviction`, `border_background_relative_wid`, `border_create_background_window`, `border_set_blur_radius`, `border_draw_background`, `border_update_background` (ending `return created \|\| frame_changed;`), plus the tail of `border_destroy_window` (`border->border_blur_radius = 0; }`) | `border->tags_applied = false; border->applied_sticky = false;` | PR's two reset lines belonged at the tail of `border_update_background`; place them right before its closing `}` |
| **B2** | 301–499 | `border_add_gradient_glow_path`, `border_draw_gradient_glow`, `border_animation_glow_blur`, `border_double_layer_centers`, `border_draw_double_layer`, `border_draw_multi_color` | `border_clear_ring`, `border_clear_thickness`, `border_clear`, and **PR's `static void border_draw_slow(...) {` header** | The common body (lines 548–822) becomes `border_draw_slow`'s body — it is our full-feature draw logic (shimmer, state colors, glow, gradient, double-layer, multi). **Drop our old `border_draw` header (lines 497–498).** The surviving `border_draw` is the nine-slice dispatcher at line 856 (common region), which already routes to `border_draw_slow`. |
| **B3** | 634–643 | Our `if (!is_double) { CGContextSetLineWidth(border->context, effective_border_width); }` | `border_clear(border, frame, settings);` in place of the full `CGContextClearRect(border->context, frame)` | `border_clear` handles ring-only clearing; it reads `fresh_surface`/`interior_painted`/`show_background`. Keep effective (animated) width, not PR's plain `settings->border_width` |
| **B4** | 800–814 | Our `drawing_draw_filled_path(border->context, inner_clip_path, border_background_color(settings, border->focused));` | `border->interior_painted = true;` (and PR's `color_style = settings->background;` + SOLID/GLOW guard lines) | Set `interior_painted` so the next `border_clear` does a full clear. Both background fill implementations agree on the fill itself; ours is the superset (respects overrides) |
| **B5** | 946–956 | Our `border->border_blur_radius = UINT32_MAX; border_recreate_context(border);` | `border->fresh_surface = true;` and `if (!border->use_layer) { ...context creation... }` | Guard context creation for layer borders. Keep our `border_recreate_context` call but make it conditional: for `use_layer` borders it should not create a CG backing context (SLWindowContextCreate on a type-5 window is at best useless, at worst harmful) |
| **B6** | 964–972 | Our `if (!settings->enabled) { border_hide(border); return; }` | PR's `if (border->external_proxy_wid) { border->stale_props = true; return; }` | Combined result: proxy check first (sets stale_props), then the enabled check |
| **B7** | 988–1010 | Our `background_host`/`background_blur_radius` computation, `border_destroy_background_window` when not companion, border-blur-radius set, and `if (border->anim_origin_override) border->origin = border->anim_current_origin;` | `bool refetch_props = border->stale_props \|\| !border->wid;` with its comment | `refetch_props` is used by the common code below (line 1027) — it must land |
| **B8** | 1069–1081 | Our `CGError shape_error = ...` capture (checked at line 1083) | PR's `if (!border->use_layer) SLSWindowFreezeWithOptions(...)` guard | Combine: freeze only when not a layer border, then `SLSSetWindowShape` capturing `shape_error` |
| **B9** | 1104–1110 | — | PR's `if (!transaction) { if (disabled_update) SLSReenableUpdate(cid); return; }` | Ours was `if (!transaction) goto cleanup;` — take PR's to avoid skipping the update re-enable. This orphans our `cleanup:` label (see B10) |
| **B10** | 1183–1193 | Our unconditional `SLSSetWindowTags`/`SLSClearWindowTags` for the border window + background window (when `update_background_placement`) | — | **Remove the `cleanup:` label** — after B9 there are no `goto cleanup` references left (verify with `rg "goto cleanup"`). Watch scope: `set_tags`/`clear_tags` are declared inside the common `if` block at lines 1168–1169, so the re-set at 1184 would be out of scope. Cleanest: keep only the background-window tag lines (or move the declarations up). The border re-set is redundant with lines 1176–1177 |
| **B11** | 1425–1473 | (ours is empty here — this is the tail of our `border_unhide`) | Only `border_invalidate_props` (lines 1429–1433) | **Drop PR's duplicate `border_hide` (1435) and `border_unhide` (1451)** — our versions at 1366 and 1415 already exist, handle the background/companion window, and schedule eviction. `border_invalidate_props` is mandatory: it is referenced by windows.c common regions (337, 345, 517), `windows_window_refresh`, and `windows_update_all` |

**After the 11 blocks, wire the layer path correctly:**

1. **Add the style-expressibility gate** in `border_create_window` (around line 927) before creating the layer border. The current code only checks `force_cg`/`layer_failed`/`unmanaged`/`is_proxy` — it does NOT check the style, so it would create a layer border for multi-color / double-layer / shimmer / animated styles and render them wrong. Gate on CG when ANY of these holds:
   - `settings->active_window.layer_count == 2` or `inactive_window.layer_count == 2` (double-layer), or
   - the active/inactive style `stype == COLOR_STYLE_MULTI`, or
   - `settings_shimmer_enabled(settings)`, or
   - `settings->animation != 0` or `settings->inactive_animation != 0` (fade/ramp/pulse are CG-context effects; slide animates origin so it is the only one the layer path could carry — simplest correct call is all animations → CG),
   - and (conservative — see Risks) any `blur_radius`/`active_blur_radius`/`inactive_blur_radius` > 0.
2. **Fix the compile error in `border_layer_style`** (line 891): `style.color = border->focused ? settings->active_window : settings->inactive_window;` assigns a `struct border_appearance` to a `struct color_style` — it will not compile. Change to `...active_window.layers[0] : ...inactive_window.layers[0];`.
3. Confirm the lifecycle wiring that is already in the common regions and connects after resolution: `border_destroy_window` (76–83) calls `layer_border_destroy`; `border_create_window` (919–961) creates via `layer_border_create` and sets `use_layer`; `border_update_internal` (1094–1101) falls back to CG when `layer_border_update` fails (`layer_failed` prevents recreation loops); `border_layer_style` (889–907) + `border_layer_draw` (909–917).

Suggested expression for the gate, kept minimal:

```c
static bool border_layer_style_expressible(struct border* border, struct settings* settings) {
  if (settings_shimmer_enabled(settings)) return false;
  if (settings->animation != 0 || settings->inactive_animation != 0) return false;
  struct border_appearance appearance = border->focused
                                        ? settings->active_window
                                        : settings->inactive_window;
  if (appearance.layer_count != 1) return false;
  if (appearance.layers[0].stype == COLOR_STYLE_MULTI) return false;
  if (settings->blur_radius > 0.f
      || settings->active_blur_radius > 0.f
      || settings->inactive_blur_radius > 0.f) return false;
  return true;
}
```

and call it from `border_create_window` alongside the `!border_get_settings(border)->force_cg` check.

### Step 3: Verify

```sh
make          # must compile bin/borders including src/layer.m (needs the makefile FILES entry);
              # clang treats .m as Objective-C; -framework QuartzCore/-framework AppKit already in LIBS
make test     # all 14 test binaries must pass:
              #   border-radius, color-style, animation-focus, animation-tick, animation-parse,
              #   windows-regression, shimmer-state, border-geometry, window-space, settings-ownership,
              #   parser-bounds, message-payload, config-execution, mach-message
```

- Confirm constants used by layer.m exist in our border.h — verified already: `BORDER_TSMN` (border.h:18), `BORDER_STYLE_ROUND_UNIFORM` (border.h:14), `BORDER_STYLE_SQUARE` (border.h:15).
- `make check` (syntax-only over `$(FILES)`) is a good extra pass since it now includes `src/layer.m`.
- Manual smoke test: launch with `renderer=layer` and confirm solid/gradient/glow borders render and resize cheaply; run with `renderer=cg` and confirm multi-color/double-layer/shimmer still work; run with default settings and confirm the auto-fallback lands on CG for the non-expressible styles.

### Step 4: Review and land

- Review the full diff vs `main` — both sides preserved, no feature regressions (background/blur, double-layer, multi-color, shimmer, animations, filter tables).
- Merge `rewrite/layer-rendering` into `main` (fast-forward), push to the **`wiggly-sheets` remote** (NOT `origin` — origin is FelixKratz upstream with no push access; the user's repo is `wiggly-sheets/JankyBorders`).

---

## 5. Risks / watch-items

- **Duplicate `border_hide`/`border_unhide`** — confirmed our definitions exist (border.c 1366/1415) and are declared in border.h outside conflict markers. Drop PR's B11 duplicates, or you get redefinition errors.
- **Orphaned `cleanup:` label** in border.c after B9 resolution — confirmed it exists (B10, line 1191) and B9's `goto cleanup` is the only reference. Delete it; verify with `rg "goto cleanup"`.
- **`set_tags`/`clear_tags` scope in B10** — declared inside the common if-block (1168–1169), referenced again by our B10 lines; re-declare or restrict to the background-window tags.
- **Style-expressibility gate is MISSING in the current tree** — without it multi/double/shimmer silently take the layer path and render garbage. This is the highest-risk item; do not land without it.
- **`border_layer_style` type mismatch** — `struct border_appearance` vs `struct color_style` assignment does not compile; must use `.layers[0]`.
- **Layer path + background blur** — unverified behavior. A type-5 layer window has no backing store; `SLSSetWindowBackgroundBlurRadius` on it is untested. Conservative choice (recommended above): force the CG path when any blur radius is set, so blur always rides on the proven backing-store path.
- **`layer.m` includes `border.h`** — compiles against our larger `struct border` and `struct settings`; the structs have already auto-merged so this is satisfied as long as C1 (border.h) brings in `show_background`/`force_cg`.
- **Context creation for layer borders (B5/site 1087)** — `border_recreate_context` runs unconditionally in the frame-change path (line 1087). `SLWindowContextCreate` on a type-5 window may return NULL (guarded) or attach a useless backing context; prefer guarding the call with `!border->use_layer` for cleanliness.
- **PR is still OPEN upstream** (not merged into FelixKratz main) — we are carrying this ourselves; future upstream syncs may need reconciliation.
- **Behavioral sanity check on `fresh_surface`** — `border_clear` skips the full clear on a fresh surface; if the background fill path (B4) ever fails to set `interior_painted`, globs of stale interior pixels may persist. The B4 resolution (always set `interior_painted` when filling) is the mitigation.

---

## 6. Repo/remote facts

- `origin` = https://github.com/FelixKratz/JankyBorders (upstream, NO push access — git credentials authenticate as wiggly-sheets; push was denied with 403).
- `wiggly-sheets` = https://github.com/wiggly-sheets/JankyBorders (the user's fork, where pushes go).
- Local branches were cleaned up earlier: all feature/codex branches deleted locally, exist on wiggly-sheets remote. Only `main` and `pr-208` remain locally (plus remote-tracking refs).
- `main` is 48 commits ahead of `origin/main` (the audit-fixes merge).