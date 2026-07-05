# Prototype Mk1 — project information

The single reference for how this keyboard's firmware is put together and how to work on it.
Consolidated 2026-07-06 from the earlier docs set (CHANGES, PROJECT-REVIEW, REVIEW-BRIEF,
TOUCH-SCREEN-NOTES, MIGRATION_NOTES, IMPROVEMENT-PLAN, CODE-REVIEW, ZMK-3156-DEEP-DIVE). Issue
history and fixes live in the companion `issues.md`.

---

## 1. What this is

A DIY split keyboard on ZMK v0.3.0 / Zephyr 4.1 / LVGL 9.3.

| Piece | Hardware | Role |
|---|---|---|
| Halves | nice!nano v2 + nice!view (gem layout) | BLE peripherals; static display frame |
| **Dongle** ("waveshare") | Seeed XIAO nRF52840 + Waveshare 1.69" LCD (280×240, ST7789V, glass corners R5.15mm ≈ 44px) + CST816S touch | Split central; colour status screen + touch UI + trackpad |
| Old dongle ("nano") | nice!nano v2 + nice!view | Legacy central, still buildable |

The dongle runs the OPERATOR layout from a fork of `carrefinho/prospector-zmk-module`.
Firmware is built by GitHub Actions (`build.yaml`); flash the artifact per target.

## 2. Three repos, one pin discipline

| Repo | Branch | Holds |
|---|---|---|
| this repo | `main` | shields, keymap, `app/src/touch/touch_input.c`, `config/west.yml` |
| `H0N3YC4T/prospector-zmk-module` | `feat/new-status-screens` | the whole on-screen touch UI (`status_screen.c`, `brightness.c`, widgets) |
| `H0N3YC4T/zmk` | `fix/3156-deferred-subscribe` | split-central reconnect fix (`central.c`) |

Both forks are pinned by **exact SHA** in `config/west.yml`. `west.yml` has three `revision:`
lines (zmk first, prospector last; the middle is zmk-dongle-display `2bb333f8`). Fork edit cycle:
commit fork → push → replace the pin (assert the new SHA occurs exactly once — a bulk replace
once hit the wrong line) → commit + push this repo → CI. Local working clones live in
`_touchref/`; the SHA in `west.yml` is the source of truth, not the clones.

**Fork rebase onto a newer upstream** (both branches are linear on a clean base):
```
cd _touchref/zmk         # or _touchref/prospector
git fetch origin
git rebase origin/main <fork-branch>
# resolve (prospector conflicts are wholly ours -> take ours), then:
git push -f fork <fork-branch>
# bump the pin in config/west.yml, full CI, and (zmk) hardware-retest reconnect before trusting
```
If upstream refactors zmk `central.c`, re-port by hand the four elements: (1) deferred
`split_central_flush_subscriptions()` after the walk; (2) per-subscription `disc_params`
(position/sensor/battery each own their CCC discover struct); (3) `release_peripheral_slot()`
clears value + ccc handles; (4) position-state subscribe self-heal (disconnect-on-failure).

## 3. Touch stack architecture

```
CST816S (I2C) --Zephyr input_cst816s--> touch_input.c (this repo)
   INPUT_ABS_X/Y + INPUT_BTN_TOUCH          |  taps: raw screen coords
                                            v
                          prospector_touch_tap(sx, sy)     [weak here, strong in the fork]
                          prospector_touchpad_active()     [true while the MOUSE page shows]
                                            v
                        status_screen.c (fork): views, grids, key sends, rendering
```

- `touch_input.c` owns the **panel→screen transform** and all **gesture recognition** (tap and
  trackpad state machine). It knows nothing about views. The transform is 90° CW:
  `sx = ty`, `sy = PANEL_W − tx` (panel 240×280 → screen 280×240). A `tp_flip` flag inverts both
  axes (`SCREEN_W/H−1 − v`) when the display is rotated 180° (see §5).
- `status_screen.c` owns **views + cell mapping** (`grid_rows`/`grid_cols` per view: 2×3, 3×3,
  or 4×4) and all **rendering**. It knows nothing about the sensor.
- The seam is weak symbols, so this repo builds green with or without the fork.

**CST816S facts (re-learn before touching gestures):**
- Single-touch only — two-finger gestures are physically impossible (scroll is a right-edge lane).
- Re-reports `BTN_TOUCH=1` every cycle while held → down/up must be edge-triggered
  (`evt->value && !active` / `!evt->value && active`).
- Carries `evt->sync` on BTN_TOUCH, NOT on ABS_X/Y → motion must be sampled on ABS events.
- Wiring: SDA→D4, SCL→D5 on the shared `&i2c1` bus (alongside the prospector apds9960 @0x39),
  IRQ→D0, RST→D1; node `cst816s@15` in `prototype_mk1_waveshare.overlay`.

**Threading invariant (the rule that has bitten twice):** three contexts exist — the input
callback (driver), the LVGL display thread (`ui_timer_cb`, 30 ms), the system workqueue. ALL
`zmk_hid_*` / `zmk_endpoint_*` / behavior invokes happen ONLY on the system workqueue. Marshalling:
atomics + `k_work_submit` from the input callback; an 8-deep SPSC ring (`key_ring`) +
`k_work_submit` from the display thread. Note `zmk_behavior_queue_add(wait=0)` is **not** a thread
hop — it runs synchronously on the caller — so the hop must come from our own `k_work_submit`,
with `queue_add` called inside that handler.

## 4. UI navigation + design language

Navigation map (2×3 grid, cells 0..5 = row*3 + col; post-2026-07-06 menu swap):
```
NORMAL --tap anywhere--> HOME
HOME:     0-2 = BACK->NORMAL | 3 = TRACKPAD | 4 = SETTINGS | 5 = KEYS hub
SETTINGS: 0 sens+ | 1 back | 2 bright+ | 3 sens- | 4 rotate 180deg | 5 bright-
HUB:      0 Fn(F-keys) | 1 back | 2 123(numpad) | 3 #$%(symbols) | 4 MEDIA | 5 MOD
MEDIA:    0 vol- | 1 back->HUB | 2 vol+ | 3 prev | 4 play/pause | 5 next
FKEYS/SYMBOLS: 3x3 paginated, 7 keys/page; cell 1 = Back(pg0)/Prev, cell 7 = Next
NUMPAD:   4x4; 7 8 9 + / 4 5 6 - / 1 2 3 * / back 0 enter / ; operators (col 3) blue
MODIFIERS: one-shot Ctrl/Shift/Alt/Gui; armed = solid blue fill + black text
TRACKPAD: whole-screen pointer; exit -> HOME (top-left corner tap)
```
- **Idle timeout** (`TOUCH_TIMEOUT_MS` = 30 s) returns only HOME/SETTINGS to NORMAL. Everything
  from `VIEW_HUB` onward in the enum (hub, media, trackpad, key pages) never times out — media
  and trackpad are placed after `VIEW_HUB` deliberately, exit is always explicit.
- **Colour roles** (reuse the OPERATOR theme): lilac `COLOR_ACCENT` = keys, red `COLOR_BACK` =
  back/exit, pastel blue `COLOR_PAGE` = nav + numpad operators + armed/rotate. Greyed control =
  `COLOR_HINT_GLYPH`. Named greys: `COLOR_HINT`, `COLOR_HINT_GLYPH`, `COLOR_LANE_BG/EDGE`,
  `COLOR_BTN_BG` (0x101216 charcoal fill).
- **Navigation glyphs:** red ▲ = back/up a level (all back buttons), blue ▲/▼ = prev/next page.
  Menu items are LVGL built-in FontAwesome glyphs (audio/settings/keyboard/GPS-cursor); Fn, 123,
  #$%, MOD stay text (no glyph exists).
- **Glass corners:** R5.15mm ≈ **44px** (`GLASS_RADIUS`). The button grid is inset by `UI_PAD`
  (5px) so corner buttons clear the arcs; full-screen frames (armed-mod frame) use radius 44.
  Touch zones still span the full screen — only drawing is inset. One font: `lv_font_montserrat_20`.
- **Armed one-shot modifiers:** solid blue fill + black text on the button, plus a blue
  radius-44 frame (a child rounded-rect, not an overlay border) around the whole screen.
  Leaving the hub clears `pending_mods` so an armed mod can't leak into normal typing.
- Key sends reuse ZMK's `&kp` (`behavior_dev="key_press"`, `param1 = keycode | (pending_mods<<24)`)
  — no per-key macros.

## 5. Trackpad, sensitivity, and rotation

- **Gestures** (in `touch_input.c`): drag = pointer; 1 tap = left click (deferred `TP_DTAP_MS`
  180 ms — the cost of also having 2-tap = right click); 2 taps = right click; top-left 40px
  corner tap = exit (cancels any pending first-tap); far-right strip
  (`TP_SCROLL_ZONE_X` = 240, drawn flush to the screen's right edge) = vertical scroll
  (`TP_SCROLL_PX` per tick).
- **Sensitivity** (replaced the old acceleration curve 2026-07-06): flat x256 multiplier,
  `level * TP_SENS_MULT256 / 256`, with per-touch x256 carry (`tp_carry_x/y`) so slow diagonals
  keep sub-pixel travel. Level range 0..`TP_SENS_MAX` (10), default `TP_SENS_DEFAULT` (5) ≈ 4×
  movement; 10 ≈ 8×, 0 holds still. Adjusted from the SETTINGS left column via
  `prospector_touchpad_sens_get/step` (in-RAM, resets to default on reboot).
- **180° rotation** (SETTINGS cell 4): `settings_toggle_rotation()` flips the ST7789V between its
  two landscape orientations (`display_set_orientation` ROTATED_90 ↔ ROTATED_270 — LVGL's 280×240
  geometry is unchanged), calls `prospector_touch_set_flip()` to invert the touch mapping, then
  `lv_obj_invalidate(lv_screen_active())` for a full redraw. Two taps = full circle. 90° is not
  offered — it would swap to a 240×280 portrait geometry and break every widget position.
- **Tuning knobs** (top of the respective files): `TP_SENS_MULT256`, `TP_SCROLL_PX`, `TP_DTAP_MS`,
  `TP_MOVE_DEADZONE_PX`, `TP_CORNER_PX`, `TP_SCROLL_ZONE_X` (must match the fork's rendered lane).

## 6. Brightness & settings specifics

- Display dimmer (`brightness.c`): `prospector_brightness_step/get`, binds the display's pwm-leds
  controller specifically via `DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)))` (there are two
  pwm-leds nodes — see §9). Clamps 5–100%. Never touches the keyboard `&bl` relay.
- SETTINGS +/- control cells show the live value (sensitivity 0..10, brightness %) and grey out
  (`COLOR_HINT_GLYPH`) at their end stop (`SETTINGS_SENS_MAX` / `SETTINGS_BRIGHT_MIN`=5/`_MAX`=100).
  Volume lives on the MEDIA screen.

## 7. Build, flash, and reset

- Matrix in `build.yaml` (board ids `nice_nano//zmk` for the halves/nano dongle, `xiao_ble//zmk`
  for the waveshare dongle). Flash `dongle-waveshare` for the dongle; `left`/`right` for the halves.
- **Reset ritual:** a UF2 flash does NOT wipe NVS. Stale BLE bonds on the XIAO cause
  "Security failed … err 2" → no keys. The `reset-waveshare` artifact (xiao_ble settings_reset)
  wipes the XIAO's bonds; wipe all three devices then re-pair dongle → left → right.
- **Dongle RAM is tight.** `CONFIG_LV_Z_MEM_POOL_SIZE=28672` (the 4×4 numpad ≈ 32 LVGL objects
  exhausted the 20 KB OPERATOR default → NULL `lv_obj_create` → crash; 32768 overflowed RAM by
  ~5 KB, so RAM is the ceiling). Funded partly by right-sizing the behavior queue:
  `build.yaml` dongle cmake-args `-DCONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=64` (global conf sets 512 for
  the halves' long macros; cmake-args is the only lever that beats the last-merged global conf —
  do NOT move the 512 out of the global conf). Every LVGL alloc is NULL-guarded (skip the cell,
  don't crash); if buttons render missing, the fix is a shared `lv_style_t`, not a bigger pool.

## 8. Peripheral display (halves)

`boards/shields/nice_view_gem/Kconfig.defconfig`:
- `NICE_VIEW_ANIMATION` default **n** — single static frame instead of the animation loop
  (battery; the user doesn't watch the halves). `=y` restores the loop.
- `NICE_VIEW_GEM_TRANSMUTATION_ONLY` default **y** on main — compile only the transmutation
  theme's bitmaps (slim peripheral flash) and lock the gem to it. Visually identical to `=n` with
  animation off. The full 6-theme + `cycle_animation` hotkey system is preserved on branch
  `dev/periph-theme`; the peripheral hotkey-relay fix is on tag `archive/dev-fix-theme`. To
  restore switching: `ONLY=n`, `ANIMATION=y`.

## 9. Split reconnect (current state)

Reconnect-after-sleep (a half links but sends no keys until the dongle is power-cycled) is ZMK
**#3156**. Two layers, both in place:
- Buffer margin: `CONFIG_BT_ATT_TX_COUNT=20` in `prototype_mk1_waveshare.conf` (ZMK PR #3216
  defaults 10 for a central; our pinned zmk includes it). Necessary but proved **insufficient
  alone** — the binding constraint is discovery concurrency, not buffer count.
- The real fix (zmk fork `central.c`): defer all GATT subscriptions until the characteristic walk
  completes (no nested CCC discovery racing the walk), give sensor + battery their own
  `disc_params`, self-heal by disconnecting on a failed/absent position-state subscription, and
  clear cached handles on slot release. Full root-cause analysis is in `issues.md`.

`CONFIG_ZMK_SLEEP=n` on the dongle is correct hygiene (USB-powered, stays awake to accept
reconnects; also sidesteps the separate #3207 central-sleep regression) — not itself the #3156 fix.

## 10. Deliberate hacks (documented, not accidents)

- **Dummy encoders + `zmk,keymap-sensors` node** on the dongle overlay (EC11 on unrouted
  P1.00–P1.03): the dongle has no physical encoders, but the central compiles out ALL sensor
  handling unless `ZMK_KEYMAP_HAS_SENSORS` is true in ITS build, which needs this DT node —
  without it the halves' encoders are silently dropped.
- **Backlight relay placeholder** (pwm0 on P0.08, an unconnected pad): keeps `&bl` (GLOBAL
  locality) relaying to the halves and satisfies ZMK's `zmk,backlight` BUILD_ASSERT; moved off
  D0/P0.02 when D0 became the touch IRQ. Separate from the display's `disp_bl` (pwm1) — never
  point `zmk,backlight` at the display or the two weld.
- **Trackpad hotspots are code constants, not rendered zones**: corner-exit (40px) and scroll
  lane (x≥240) live in `touch_input.c`; the fork only draws the affordances. Change one → change
  both.

## 11. Branch & tag map (post-2026-07-05 cleanup)

| Ref | Purpose |
|---|---|
| `main` | The live config. Everything merged in. |
| `zmk-0-3` | Historical pre-touchscreen config, keymap refreshed to current. |
| `dev/periph-theme` | Restore point for the full 6-theme + `cycle_animation` system. |
| tags `archive/dev-touch-easy`, `archive/dev-touch-testing`, `archive/dev-fix-theme` | Heads of deleted branches, kept forever. |
| fork `prospector…/feat/new-status-screens`, `zmk…/fix/3156-deferred-subscribe` | Pinned by west.yml — never delete. |

## 12. Feature history (work orders, all shipped)

The touch UI was built as WO-1..11: RAM reclaim (WO-1), brightness readout (WO-2), armed-mod
indicator (WO-3), fork rebase procedure (WO-4), branch hygiene (WO-5), docs consolidation (WO-6,
this doc), conf comment parity (WO-7), hub label polish (WO-8), home MACRO→KEYS + wider buttons
(WO-9), 3×3/4×4 grid generalization with numpad operators (WO-10), trackpad hub button +
full-screen pointer page (WO-11). Later: icon menus, chevron nav, visual polish for the glass,
whole-screen trackpad with right-edge scroll, pointer sensitivity, 180° rotation, the menu swap
(media↔trackpad), and the settings rework.

## 13. Tuning-knob quick reference

| Want to change | Where |
|---|---|
| Pointer speed | `TP_SENS_MULT256` (`touch_input.c`), or the settings sensitivity level at runtime |
| Scroll speed | `TP_SCROLL_PX` (`touch_input.c`) |
| Left-click latency / double-tap window | `TP_DTAP_MS` (`touch_input.c`) |
| Scroll lane width / position | `TP_SCROLL_ZONE_X` (`touch_input.c`) + the lane render (`status_screen.c`) |
| Menu idle timeout | `TOUCH_TIMEOUT_MS` (`status_screen.c`) |
| Brightness step / limits | `BRIGHTNESS_STEP`, `SETTINGS_BRIGHT_MIN/MAX` (`status_screen.c`), clamp in `brightness.c` |
| Glass corner radius (armed frame / insets) | `GLASS_RADIUS`, `UI_PAD` (`status_screen.c`) |
| Colours | `COLOR_*` block (`status_screen.c`) + `display_colors.h` |
| Behaviour-queue / LVGL pool sizes | `build.yaml` cmake-args + `prototype_mk1_waveshare.conf` |
