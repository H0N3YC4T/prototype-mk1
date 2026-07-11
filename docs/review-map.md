# Code review map — everything since the Zephyr 4.1 / LVGL 9.3 uplift

> **HISTORICAL SNAPSHOT (2026-07-08).** Written for the manual review of that date; repo
> names, paths and file lists predate the mk1 renames, the nice-view extraction and the
> declarative touch-UI rework. `docs/issues.md` it cites was retired (see git history).
> Kept for reference — do not use as a current map.

A guide for manually reviewing the work done since the firmware moved to ZMK's Zephyr 4.1 /
LVGL 9.3 base. **Current-state only** — anything that was tried and later reverted or superseded
(widget-shrink experiment, the old 180°-only rotation, the acceleration curve, swipe-to-back,
the vendored `dongle_display` shield) is deliberately excluded; this lists what actually stuck.

**Boundary:** commit `cd13044` "Migrate build plumbing to Zephyr 4.1 (reset canary)"
(2026-06-23) in `Keyboard-Prototype_Mk1`. Everything below landed at or after it.

## What's involved

Three code locations:

1. **`Keyboard-Prototype_Mk1`** (this repo) — build plumbing, the two hardware shields
   (`prototype_mk1` halves + dongle overlays, `nice_view_gem` halves display), themes, keymap,
   docs. Net change vs pre-migration: **83 files, ~7,200 insertions / ~4,600 deletions.**
2. **`zmk-waveshare-touch`** (sibling repo, `../zmk-waveshare-touch`) — the touch-dongle module
   (XIAO + Waveshare 1.69"). **Entirely post-migration** and entirely custom on top of
   carrefinho/prospector-zmk-module. ~3,600 lines of hand-written C. Consumed here via an
   exact-SHA pin in `config/west.yml` (currently `596a6af`).
3. **`H0N3YC4T/zmk` fork** (external, pinned in `config/west.yml` at `c2ca24f`) — one targeted
   patch to `app/src/split/bluetooth/central.c` (the BT reconnect fix). Not checked out locally;
   review on GitHub or in the west workspace.

## How to read this map

Each file is tagged with review intensity:

- 🔴 **read closely** — hand-written logic, correctness-critical, or where the bugs would hide.
- 🟡 **skim** — real changes but mechanical/low-risk (config, wiring, layout constants).
- ⚪ **skip line-by-line** — generated bitmap/font asset arrays; confirm they're data, move on.

## Suggested review order (highest-risk first)

1. Trackpad gesture driver — `zmk-waveshare-touch/.../touch/touch_input.c` (the state machine +
   threading).
2. Touch↔keyboard threading seam — same file + `touch_views.c` + `status_screen.c`.
3. BT reconnect fix — the `H0N3YC4T/zmk` fork's `central.c` + `config/west.yml` pin rationale.
4. Display driver patch — `zmk-waveshare-touch/drivers/display/display_st7789v.c`.
5. Rotation (spans both repos, one shared assumption) — `touch_rotation.c` +
   `display_rotate_init.c` + `status_screen.c` reflow.
6. Everything else (themes, keymap, widgets, build plumbing).

---

# Part 1 — `zmk-waveshare-touch` module (the biggest and newest surface)

All paths below are relative to `../zmk-waveshare-touch/`. The module's own
[`CHANGES.md`](../../zmk-waveshare-touch/CHANGES.md) carries the full rationale/history — but see
the ⚠ note under Rotation: its rotation section is stale.

## 1a. Trackpad + touch UI  — `boards/shields/prospector_adapter/src/touch/`

The single biggest and most correctness-sensitive area.

- 🔴 `touch/touch_input.c` (434 lines) — the CST816S gesture driver / state machine.
  `enum tp_mode { TP_PENDING, TP_MOTION, TP_SCROLL, TP_DRAG }`. Handles: pointer motion, 1-tap
  left / 2-tap right click, right-edge scroll lane, and the new **drag-lock** (tap-then-hold-and-
  drag). **Review focus:**
  - *Threading:* every `zmk_hid_*` / `zmk_endpoint_*` call must run on the system workqueue via
    `k_work_submit` (`tp_work_handler`), never in the input callback (driver context). The
    click/drag commands cross that boundary through the `tp_click` / `tp_drag_cmd` atomics.
  - *Stateful vs transient HID:* mouse buttons persist until explicitly released (drag-lock
    relies on this — press once, hold across N motion reports, release on lift); movement/scroll
    axes are zeroed each send. Confirm the press/release pairing in `tp_work_handler` +
    the release handler's `TP_DRAG` branch.
  - *Edge-triggering:* CST816S re-reports `BTN_TOUCH` every cycle while held; the driver must
    edge-trigger touch-down (`evt->value && !active`). Sync is carried on `BTN_TOUCH`, not ABS.
  - *The drag-lock race I fixed:* `tp_tap_work` (deferred single-click fallback) is cancelled
    eagerly when the 2nd touch begins, not just at its resolution — otherwise pausing mid-hold
    could fire a stray left-click. Worth tracing yourself.
  - *Sub-pixel motion:* `TP_SENS_MULT256` fixed-point scaling with `tp_carry_x/y` accumulators.
- 🔴 `touch/touch_views.c` (347 lines) — the registry-driven view system. `view_defs[VIEW_COUNT]`
  declares each view's `build` / `on_tap` / grid dims / `portrait_map` / `idle_timeout` /
  `keeps_mods`. Portrait re-arrangement table `p23_pos[6] = {0,2,1,4,3,5}`. **Review focus:** the
  tap-dispatch coordinate mapping (raw 280×240 → grid cell), and that `prospector_touchpad_active()`
  correctly gates trackpad-mode vs tap-mode.
- 🟡 `touch/touch_draw.c` (94), `touch/touch_keys.c` (59), `touch/touch_nav.c` (102) — cell
  drawing, key-send ring, nav/menu transitions. Skim for the SPSC `key_ring` drain and the
  armed-modifier encoding (`keycode | mods<<24`).
- 🟡 `touch/touch_ui.h` — shared declarations / tuning constants.
- 🔴 `src/status_screen.c` (84) — entry point; `status_screen_reflow()` does the per-orientation
  layout. Small but central. **Review focus:** the landscape-vs-portrait branch and the modifier
  row width (200 portrait / 230 landscape).

## 1b. Rotation (⚠ stale doc — trust the code)

- 🔴 `touch/touch_rotation.c` (43) — the **4-step** rotation: `ui_rot` (0–3), `rot_to_panel[]`,
  `settings_apply_rotation()`. Steps 90° CW per tap through all four orientations.
- 🟡 `src/display_rotate_init.c` (24) — boot orientation, hardcoded to `ROTATED_270` (the
  Kconfig `..._ROTATE_DISPLAY_180` branch was removed as a desync landmine).
- The module `CHANGES.md` rotation section previously described the OLD 180°-only
  `settings_toggle_rotation()` / `prospector_touch_set_flip()` model — **corrected 2026-07-08**
  (same day as this repo's `docs/information.md` §5). Both docs now match the 4-step code.

## 1c. Display driver patch  — `drivers/display/`

- 🔴 `drivers/display/display_st7789v.c` (584) — patched ST7789V driver (280×240 panel,
  orientation/offset handling for the Waveshare glass). Mostly upstream Zephyr driver; **review
  the deltas** (MADCTL / column-row offset / orientation) rather than the whole file. Pair with
  `.h`.

## 1d. Brightness  — `src/brightness.c` 🔴 (183)

- `prospector_brightness_step/get()`. **Two real fixes to check:** (1) binds the display's
  pwm-leds controller specifically via `DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)))` — the
  keyboard build has a *second* pwm-leds node, and the old `DEVICE_DT_GET_ONE(pwm_leds)` could
  bind the wrong one (brightness went to a dead pad). (2) ALS fade loop rewritten with signed
  math (upstream stepped a `uint8_t` and checked `< 0` — never true). Clamps 5–100%.

## 1e. ZMK-level vendored behaviors/events  — `src/behaviors`, `src/events`, `src/split`

- 🔴 `src/split/bluetooth/central_status_changed_observer.c` (194) — split central status
  observer (feeds the dongle's connection UI). Review the subscription/lifecycle.
- 🟡 `src/behaviors/behavior_caps_word.c` (197) — caps-word behavior (OPERATOR layout). Largely
  from the prospector base; skim for local deltas.
- ⚪ `src/events/caps_word_state_changed.c`, `src/events/split_central_status_changed.c` (3 lines
  each) + their headers — boilerplate event definitions.

## 1f. Widgets  — `src/widgets/` (mostly upstream OPERATOR theme)

Per the module's CHANGES.md these are the upstream prospector OPERATOR widgets, **untouched
except where noted.** Lower priority — the one with real hand edits is `modifier_indicator`.

- 🔴 `widgets/modifier_indicator.c` / `.h` (142) — font → `lv_font_montserrat_16`; added
  `zmk_widget_modifier_indicator_set_width()` for per-orientation width (200/230). Review this.
- 🟡 `widgets/wpm_meter.c` (239), `widgets/battery_circles.c` (543), `widgets/output.c` (180),
  `widgets/layer_display.c` (74) — shrink experiments here were **reverted to original sizes**;
  net change is minimal. Confirm they're back to full size (WPM bar 90, arc 58, buttons 29) and
  skim their `set_width`/reflow hooks.
- ⚪ `widgets/display_colors.h`, `widgets/battery_circles.h` etc. — color roles / declarations.

## 1g. Board / shield plumbing  — 🟡 skim

- `boards/shields/prospector_adapter/{CMakeLists.txt, Kconfig, Kconfig.defconfig, Kconfig.shield,
  prospector_adapter.conf, prospector_adapter.overlay}` — shield wiring. Note
  `CONFIG_LV_FONT_MONTSERRAT_16=y` (for the modifier font) and `LV_Z_MEM_POOL_SIZE` sizing
  (28672 — the 4×4 numpad exhausted the 20 KB default and crashed; 32768 overflowed RAM).
- `boards/shields/prospector_adapter/boards/xiao_ble_zmk.overlay` — CST816S + display DT nodes.
- `include/{fonts.h, modifier_order.h, symbols.h}`, `src/modifier_order.c` (100) — HID/label
  tables.
- `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml`, `dts/bindings/zmk,prospector-theme.yaml` —
  module manifest / build.
- ⚪ `src/fonts/*.c` (DINish/FoundryGridnik glyph arrays) — generated font data, skip.

---

# Part 2 — `Keyboard-Prototype_Mk1` (this repo)

## 2a. Build plumbing & migration  — 🟡 skim, but the load-bearing part

- `config/west.yml` — **read the pin rationale comments** (🔴). Three pins: the `H0N3YC4T/zmk`
  fork (`c2ca24f`, reconnect fix), `englmaxi/zmk-dongle-display` (`2bb333f`), and
  `zmk-waveshare-touch` (`596a6af`). This is the manifest that ties everything together.
- `build.yaml` — build targets (settings_reset, dongle, waveshare, central/periph halves).
- `.github/workflows/build.yml`, `CMakeLists.txt`, `.gitignore` — CI + top-level build.
- Board-id migration: `nice_nano_v2` → `nice_nano//zmk` (HWMv2); overlay rename
  `nice_nano_v2.overlay` → `nice_nano_nrf52840_zmk.overlay`.
- **Deleted:** the entire vendored `boards/shields/dongle_display/` tree (~30 files) — replaced
  by the `englmaxi/zmk-dongle-display` module. Nothing to review beyond confirming it's gone and
  nothing references it.

## 2b. BT reconnect fix (the highest-stakes single change)  — 🔴

- Lives in the **`H0N3YC4T/zmk` fork**, `app/src/split/bluetooth/central.c`, pinned at `c2ca24f`
  (see `config/west.yml` lines 10–17 + `docs/issues.md` S2). The fix: in the ZMK #3156 failure,
  a battery-fetch during GATT discovery starves the ATT buffer and breaks re-subscription after
  the halves sleep. Fix = subscribe **after** the char-discovery walk, not inline mid-walk. The
  real root-cause knob is `BT_ATT_TX_COUNT` (already set in our zmk), **not** `BT_MAX_CONN`.
- Review by reading `docs/issues.md` (S2) and `docs/information.md` for the writeup, then the
  fork's `central.c` diff on GitHub.

## 2c. `prototype_mk1` shield (the physical keyboard + dongle)  — 🟡 / 🔴 keymap

- 🔴 `boards/shields/prototype_mk1/prototype_mk1.keymap` + `config/prototype_mk1.keymap` — the
  keymap (behaviors, layers, combos). Read closely — this is the actual typing surface. (~190 /
  ~166 line net change.)
- 🟡 Dongle/waveshare bring-up: `prototype_mk1_dongle.{overlay,conf}`,
  `prototype_mk1_dongle_base.dtsi`, `prototype_mk1_waveshare.{overlay,conf}` (new — the touch
  dongle variant), `prototype_mk1_central_left.conf`, `prototype_mk1_periph_{left,right}.conf`.
  Review the reconnect-related conf (never-deep-sleep on the dongle, `BT_ATT_TX_COUNT`) and the
  waveshare overlay (CST816S + ST7789V nodes).
- 🟡 `prototype_mk1.{dtsi,conf,yml}`, `Kconfig.defconfig`, `Kconfig.shield` — shield definition.

## 2d. `nice_view_gem` halves display + theme system  — mixed

The custom nice!view theme/animation system (LVGL 9 port + theme switching).

- 🔴 `boards/shields/nice_view_gem/widgets/{screen,animation,animation_assets,util}.c/.h` — the
  theme engine (frame sequencing, animation state, LVGL 9 draw calls). Read the logic here.
- 🔴 `app/src/behaviors/behavior_cycle_animation.c` +
  `include/zmk/events/cycle_animation_state_changed.h` — the theme-switch behavior (next/prev/
  pause 3-way cycle). Correctness-relevant (init level was fixed POST_KERNEL earlier).
- 🟡 `widgets/{battery,layer,output,profile,wpm,screen_peripheral}.c` — per-widget LVGL-9 ports.
  Note: **peripheral halves show a static transmutation frame on purpose** (switching paused).
- 🟡 `Kconfig.defconfig`, `CMakeLists.txt`, `nice_view_gem.conf`, `assets/images.c` — wiring +
  which theme bitmaps compile in (default = transmutation).
- ⚪ **Skip line-by-line:** `assets/animations/{omnissiah.c (2085), ultramar.c (2053),
  transmutation.c (1323), evangelion.c (543), landscape.c (451), crystal.c}` — these are
  generated animation **bitmap arrays**, not logic. Confirm they're data and that
  `animation_assets.c` references them correctly; don't read the pixel arrays. (`assets/crystal.c`
  standalone was deleted — the old ~1000-line variant.)

## 2e. Docs  — 🟡 read for context, they explain the "why"

- `docs/information.md` (+248) — the living architecture doc. §5 (trackpad/rotation, just
  corrected), plus reconnect, threading, LVGL memory, feature history.
- `docs/issues.md` (+213) — the issue/fix ledger; S2 = the reconnect deep-dive.

## 2f. Deleted / gone (nothing to review, just so the map is complete)

- `boards/shields/dongle_display/` (whole tree) — replaced by the englmaxi module.
- `.vscode/` (`c_cpp_properties.json`, `settings.json`) — removed.
- `app/CMakeList.txt` — removed (typo-named leftover).

---

## Quick-reference: where the bodies are buried

If you only have time for the risky bits, scrutinize these five:

1. **`touch_input.c` threading + the drag-lock state machine** — the atomics-across-workqueue
   boundary is where a subtle bug would live.
2. **The scroll-lane coupling** — the trackpad scroll-lane divider (`touch_views.c`) and the
   gesture boundary (`touch_input.c`) both use `TP_SCROLL_ZONE`, **hoisted into the shared
   `touch_ui.h` on 2026-07-08** (previously a define in one file + bare `240` literals in the
   other, kept in sync only by a comment). Confirm both files reference the shared constant and
   no bare lane literals remain.
3. **The reconnect fix** (`central.c` in the zmk fork) — ZMK internals; the whole split works or
   doesn't reconnect after sleep on this one change.
4. **`brightness.c` device binding** — the `DT_PARENT(DT_NODELABEL(disp_bl))` fix; wrong node =
   dead brightness control.
5. **Rotation** — the 4-step system (`touch_rotation.c` + `touch_input.c`'s coordinate
   transform + `status_screen_reflow()`), one shared `ui_rot`/`tp_rot` assumption across three
   files. (Its stale 180°-flip doc in the module CHANGES.md was reconciled 2026-07-08.)
