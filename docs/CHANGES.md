# Change reference — Keyboard-Prototype_Mk1

Every change this repo carries beyond a stock ZMK user config, in one file, with the detailed
rationale and history that the code comments deliberately don't repeat. Use this (plus the fork's
own CHANGES.md in prospector-zmk-module) as the context source when editing. The architectural
map is PROJECT-REVIEW.md; this is the per-file ledger.

## The three-repo shape

| Repo | Branch | Holds |
|---|---|---|
| this one | `main` | shields, keymap, touch driver, west.yml pins |
| `H0N3YC4T/prospector-zmk-module` | `feat/new-status-screens` | the whole on-screen touch UI |
| `H0N3YC4T/zmk` | `fix/3156-deferred-subscribe` | split reconnect fix |

Both forks are pinned by exact SHA in `config/west.yml` (three `revision:` lines — zmk first,
prospector last; always replace by exact-SHA match and assert the new SHA occurs exactly once,
a bulk replace once grabbed the wrong line). Fork edit cycle: commit fork → push → bump pin →
commit here → CI. Local clones live in `_touchref/`.

## `app/src/touch/touch_input.c` — CST816S gesture driver

Two jobs: tap dispatch to the fork UI (weak-symbol seam `prospector_touch_tap` /
`prospector_touchpad_active`, so this repo builds green without the fork), and the whole-screen
trackpad while the fork's MOUSE page shows.

**CST816S hardware facts (hard-won; re-learn before touching gestures):**
- Single-touch only. Two-finger gestures are physically impossible; that's why scroll is the
  right-edge lane, not a two-finger drag.
- Re-reports BTN_TOUCH=1 every cycle while held → down/up must be edge-triggered
  (`evt->value && !active` / `!evt->value && active`). Getting the release edge wrong once
  produced taps firing while the finger was still down.
- Carries `evt->sync` on the BTN_TOUCH event, NOT on ABS_X/Y → motion must be sampled on ABS
  events. The first full-screen-trackpad attempt gated on sync and never saw motion at all
  ("can't use it as a trackpad"); smooth scroll later proved the position stream was clean.

**Coordinate transform:** screen renders 90° CW vs panel axes; `sx = ty`, `sy = PANEL_W - tx`
(panel 240x280 → screen 280x240). Calibrated on hardware; the per-tap serial logging that
supported calibration has been removed from the config (see conf notes below).

**Trackpad gestures:** PENDING → (past 8px dead-zone) MOTION or SCROLL — lane membership
(`tp_scroll_zone`, screen x >= `TP_SCROLL_ZONE_X` 240) is latched at touch-down. 1 tap = left
click deferred `TP_DTAP_MS` 180ms (the cost of also having 2-tap = right click), corner tap
(40px, top-left) = exit and cancels the deferred click (a tap-then-quick-exit once fired a
stray click after leaving the page). Known accepted quirk: tap-then-immediately-drag still
fires the deferred left click mid-drag — the tap was an intended click; cancelling would eat
clicks. `TP_SCROLL_ZONE_X` (240) must match the scroll-lane the fork renders flush to the right
edge — one constant, two repos. Tuning knobs: `TP_SCROLL_PX`, `TP_DTAP_MS`,
`TP_MOVE_DEADZONE_PX`, `TP_SENS_MULT256`.

**Pointer sensitivity (reworked 2026-07-06, replaced the acceleration curve):** flat multiplier,
x256 fixed point. Per ABS sample the motion is scaled by `level * TP_SENS_MULT256 / 256` with
x256 carry accumulators (`tp_carry_x/y`, reset per touch) so slow diagonals keep sub-pixel
travel. Level range 0..`TP_SENS_MAX` (10), default `TP_SENS_DEFAULT` (5) ≈ 4x (the requested
base); 10 ≈ 8x, 0 holds still. Adjusted from the fork's settings screen via
`prospector_touchpad_sens_get/step` (strong here, weak -1/no-op fallbacks in the fork; a plain
aligned byte, so the display-thread write needs no marshalling). In-RAM only — resets to default
on reboot, same as brightness. (The earlier threshold accel curve was dropped for a plain
sensitivity scale.)

**180° display rotation (added 2026-07-06):** the fork's settings rotate button flips the panel
between its two landscape orientations; touch_input.c exposes `prospector_touch_set_flip(bool)`
(strong), which sets `tp_flip` so `panel_to_screen_x/y` invert their output (`SCREEN_W/H-1 - v`).
That single flag rotates taps AND the trackpad consistently. In-RAM, resets on reboot.

**Threading:** the input callback runs in driver context and only touches atomics +
`k_work_submit`; all `zmk_hid_*` / `zmk_endpoint_*` / behavior invokes happen in work handlers
on the system workqueue. Off-thread HID writes corrupted the shared keyboard report in the
project's history (boot-variant wrong/dropped keys) — same rule protects the mouse report.

## `boards/shields/prototype_mk1/prototype_mk1_waveshare.overlay`

- **CST816S node** on shared &i2c1 (SDA D4 / SCL D5, IRQ D0, RST D1).
- **Backlight relay placeholder** (pwm0 on P0.08, an unconnected pad): CONFIG_ZMK_BACKLIGHT
  must stay on so the GLOBAL-locality `&bl` behaviour relays to the halves, and ZMK
  BUILD_ASSERTs a chosen zmk,backlight node. Moved off D0/P0.02 when D0 became the touch IRQ.
  The dongle's real display backlight is `disp_bl` from prospector_adapter — two pwm-leds
  nodes exist, which is why the fork's brightness.c binds by parent-of-disp_bl.
- **Six touch macros** (`touch_macro_0..5`): vol-down/mute/vol-up/play-pause/prev/next, fired
  by the fork's MEDIA and SETTINGS screens; 0-2 double as the no-fork fallback grid.
- **Dummy encoders + zmk,keymap-sensors node**: the dongle has NO physical encoders — the
  halves' encoders forward over the split SENSOR characteristic, but the central compiles out
  ALL sensor handling unless ZMK_KEYMAP_HAS_SENSORS is true in ITS build, which requires this
  DT node. Without it the dongle silently drops every encoder event ("the encoder wheels have
  stopped working"). EC11 nodes sit on unrouted P1.00–P1.03 with pull-ups — inert.

## `boards/shields/prototype_mk1/prototype_mk1_waveshare.conf`

- `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS=80`, ALS off (no APDS9960 fitted).
- `CONFIG_ZMK_SLEEP=n` — USB-powered dongle stays awake to accept reconnects.
- `CONFIG_BT_ATT_TX_COUNT=20` — the buffer half of the #3156 story (ZMK PR #3216 angle);
  necessary but NOT sufficient — the real fix is the zmk fork's deferred subscriptions.
- `CONFIG_LV_Z_MEM_POOL_SIZE=28672` — the 4x4 numpad (~32 LVGL objects) exhausted the
  20KB OPERATOR default (NULL lv_obj_create → crash). RAM is the ceiling: 32768 overflowed by
  ~5KB. Funded partly by right-sizing the behavior queue (see build.yaml).
- `CONFIG_ZMK_USB_LOGGING=n` — was =y during touch calibration; turned off 2026-07-05 because
  INF tap logs also revealed touch-typed keys over USB CDC. Re-enable TEMPORARILY to watch the
  reconnect fix's marker ("Position-state subscription established") during sleep/wake
  verification, then turn it back off.

## `build.yaml`

- `reset-waveshare` (xiao_ble settings_reset): a UF2 flash does NOT wipe NVS; stale bonds on
  the XIAO caused "Security failed ... err 2" → subscription torn down → no keys. This
  artifact is the fix ritual.
- dongle-waveshare cmake-args: `-DCONFIG_ZMK_EXT_POWER=n` (no hardware) and
  `-DCONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=64` — the global config sets 512 for the halves' long
  macros; on the dongle that static K_MSGQ costs ~12KB RAM for a queue the touch UI puts 2
  entries in per tap. cmake-args is the only lever that beats the global conf (config merges
  last). Do NOT move the 512 out of the global conf; the halves need it.

## `boards/shields/nice_view_gem/` (halves' display)

- `NICE_VIEW_ANIMATION` Kconfig, default n: single static frame instead of the animation loop
  (battery; the user doesn't watch the halves). `=y` restores the loop.
- `NICE_VIEW_GEM_TRANSMUTATION_ONLY`, default y on main: compile only the transmutation
  theme's bitmaps (slims peripheral flash) and lock the gem to it. Visually identical to =n
  with animation off. The full 6-theme + cycle_animation switching system is preserved on
  branch `dev/periph-theme`; the peripheral hotkey-relay fix is on tag `archive/dev-fix-theme`.

## `CMakeLists.txt`

Adds `cycle_animation` behavior/event sources (DT-gated, inert unless the keymap declares the
node) and `touch_input.c` (DT-gated on the cst816s node — compiles to nothing on non-touch
targets).

## `config/`

- `west.yml` — the pin discipline (top of this file).
- `prototype_mk1.keymap` — user-maintained. All custom behaviors are defined in-file, so the
  keymap ports standalone (that made the zmk-0-3 historical-branch refresh safe).

## Branches & tags (after the 2026-07-05 cleanup)

`main` (live), `zmk-0-3` (historical config, keymap refreshed to current), `dev/periph-theme`
(theme-system restore point). Deleted branches preserved as tags: `archive/dev-touch-easy`,
`archive/dev-touch-testing`, `archive/dev-fix-theme`. Fork branches are pinned — never delete.

## Open items

- Hardware-verify sleep/wake reconnect (needs USB logging temporarily =y; see conf notes).
- Optional: upstream the #3156 fix — local candidate branch `pr/3156-deferred-subscribe` +
  PR-3156.md write-up in `_touchref/zmk` (local only, pending personal review).
- ~~Pointer acceleration curve~~ — done 2026-07-06; settings screen's bottom row adjusts it
  (volume moved out; it was always duplicated on MEDIA).
- ~~Swipe-to-back gesture~~ — dropped 2026-07-06 (not wanted; chevron back buttons cover it).
  Menu idle timeout raised 5s → 30s same day (hub + trackpad still exempt).
