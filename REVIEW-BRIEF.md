# Review brief — dongle touch UI + recent bug fixes

For an independent second-pass review. Everything is on branch **`dev/touch-screen`**
(HEAD `c6633a3`), **unmerged** to `main`. All CI builds are green; most fixes are
**hardware-unverified** (called out per item). The goal of this review: find what the
first pass missed — logic bugs, threading/lifetime hazards, wrong assumptions, or
things that will bite on hardware. `CODE-REVIEW.md` has the long-form write-up (§1–§9);
this is the orientation + the "look here" list.

## Hardware / topology
- **Dongle** = Seeed XIAO nRF52840 + Waveshare 1.69" ST7789 color LCD + CST816S cap-touch,
  running a fork of `carrefinho/prospector-zmk-module` (OPERATOR theme, LVGL 9.3).
  It is the BLE **split central** for two nice!nano v2 halves and talks to the host over
  BLE (3 profiles). Also a dongleless mode exists (`central_left`).
- ZMK **v0.3.0 / Zephyr v4.1.0+zmk-fixes**. RAM on this build is tight (~within 8 KB of
  the ceiling — relevant to the LVGL fix).

## Repos & pins (`config/west.yml`)
| project | remote | revision | why forked |
|---|---|---|---|
| `zmk` | **H0N3YC4T/zmk** (fork) | `c027b34` (`fix/3156-deferred-subscribe`) | split reconnect fix in `central.c` |
| `prospector-zmk-module` | **H0N3YC4T** (fork) | `c1f730f` (`feat/new-status-screens`) | the whole touch UI + brightness |
| `zmk-dongle-display` | englmaxi | `2bb333f` | unchanged |

## What was built — the touch UI
Two-screen navigation on the dongle's color LCD (tap-driven; CST816S over `&i2c1`):
```
NORMAL --tap--> HOME → { MEDIA, SETTINGS, MACRO hub }
MACRO hub → { F-keys(paginated), Numpad(4×3), Symbols(paginated 32), Modifiers(one-shot) }
```
- **Key files:**
  - fork `boards/shields/prospector_adapter/src/layouts/operator/status_screen.c` — the entire UI.
  - fork `.../src/brightness.c` — display backlight control.
  - main `app/src/touch/touch_input.c` — CST816S decode + rotation + tap→UI hand-off.
  - main `boards/shields/prototype_mk1/prototype_mk1_waveshare.overlay` — cst816s node,
    `touch_macro_0..5` (vol/media), keyboard-backlight relay placeholder.
  - main `boards/shields/prototype_mk1/prototype_mk1_waveshare.conf` — the config knobs.
- **Key mechanics to understand before reviewing:**
  - Touch runs on two threads: CST816S callback (workqueue) → `atomic_set` stash;
    an `lv_timer` on the **LVGL display thread** does all `lv_*` work + navigation.
  - Grid is per-screen: `touch_input.c` passes **raw screen coords**; the UI maps them to
    a cell via a global `grid_rows` (2, or 4 for the numpad) + `cell_from_coords()`.
  - Key output uses **`zmk_behavior_queue_add()`** (the macro path) with
    `param1 = keycode | (pending_mods << 24)` — NOT `zmk_behavior_invoke_binding` from the
    display thread (see issue #4). Keycodes from `<dt-bindings/zmk/keys.h>`.
  - Display brightness steps `disp_bl` (PWM1/P1.11) only — separate from the keyboard `&bl`
    relay on the halves.

## Issues encountered & fixes (chronological — most from live hardware testing)

**1. Split reconnect after sleep (ZMK #3156).** Half links after deep-sleep but sends no
keys until the dongle is power-cycled. Root cause: `central.c` subscribes to characteristics
**inline during the GATT discovery walk**; the battery subscribe kicks off a nested CCC
discovery that collides with the walk → `-ENOMEM` that Zephyr reports as a *clean*
completion → position-state never subscribed. `CONFIG_BT_ATT_TX_COUNT=20` (buffer bump)
was applied but insufficient. **Fix (ZMK fork `central.c`):** defer all subscribes to a new
`split_central_flush_subscriptions()` run after the walk stops; give battery its own
`disc_params`. **Highest-risk change; HW-unverified.** Full analysis in `ZMK-3156-DEEP-DIVE.md`.

**2. Display brightness never changed.** Root cause: this build has **two `pwm-leds` DT
nodes** (the display `disp_bl`, and a keyboard-backlight relay placeholder), so
`brightness.c`'s `DEVICE_DT_GET_ONE(pwm_leds)` bound the *placeholder* (a dead pad).
**Fix:** `DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)))` + a `LOG_INF`. HW-unverified
(assumes the panel BL is actually wired to P1.11).

**3. One-shot modifier footgun.** A mod armed on the Modifiers screen persisted until a key
was sent; leaving the hub without sending left it armed → stray Ctrl+key later. **Fix:**
clear `pending_mods` on any transition to a non-hub view.

**4. Number keys "mess up," varying boot-to-boot.** Root cause: touch key-sends invoked
behaviors — writing the shared HID keyboard report — **on the LVGL display thread**, which
ZMK never locks against its own (workqueue) key path → report corruption. Consumer keys
(volume) use a separate report, so they looked fine. **Fix:** route key-sends through
`zmk_behavior_queue_add` (ZMK's own queue/context). HW-unverified.

**5. Crash when opening the numpad.** Root cause: `draw_cell` makes 2 LVGL objects/cell; the
numpad is 12 cells (~24 objects, 2× other screens) and exhausted the 20 KB LVGL pool →
`lv_obj_create` NULL → deref → fault. **Fix:** `LV_Z_MEM_POOL_SIZE` 20 KB→25 KB (32 KB
overflowed RAM by ~5 KB, so 25 KB is the safe max-ish) + NULL-guard `draw_cell`. HW-unverified.

## Verification status
- **CI:** all green (dongle-waveshare, both halves, central_left, nano dongle, reset).
- **Hardware confirmed earlier:** touch navigation, timeout, 6-cell grid, F-keys *typing*
  (before the thread-race fix), settings vol/mute.
- **NOT yet hardware-confirmed:** reconnect fix, brightness moving the panel, the
  number-key race fix, the numpad crash fix, and the Symbols/Modifiers screens at all.

## Where I'd focus the review (my own suspicion list)
1. **`central.c` deferred-subscribe (issue #1)** — the riskiest. Verify: does the `subscribed`
   early-STOP vs `attr==NULL` completion always flush exactly once? Any path where a walk ends
   without flushing? Is issuing `bt_gatt_subscribe` back-to-back for position-state then battery
   actually safe given ATT serialization, or can the 2nd fail? Does deferring change behavior for
   sensor/input-split builds (I left input-split inline)? Is calling `bt_gatt_subscribe` from the
   discovery callback at the STOP point sound?
2. **Number-key race fix (issue #4)** — is `zmk_behavior_queue_add` genuinely race-free vs the
   dongle's peripheral-key HID path, or just less likely to collide? It runs on the system
   workqueue via a delayable; confirm that's the same serialization ZMK relies on. Did I actually
   diagnose the right root cause, or could the "mess up" have been 4×3 touch miscalibration?
3. **Numpad memory (issue #5)** — is 25 KB actually enough for 24 objects + the always-present
   status widgets, or will buttons render blank (guards hide it as non-crash)? Is a shared
   `lv_style_t` the right structural fix? Any other screen close to the pool limit?
4. **`send_key` encoding** — `keycode | (pending_mods<<24)`: correct for ZMK's mod-in-high-bits
   encoding? Symbols already carry shift (`LS(...)`); does OR-ing another mod ever misbehave?
5. **Thread-safety of the tap hand-off** — `pending_tap` packs `(sx,sy)` into one atomic;
   `grid_rows` is a plain global read on the display thread and written in `build_view`. Any
   TOCTOU between a queued tap and a view change? The single-slot atomic drops a tap if two land
   in one 30 ms tick — acceptable?
6. **Brightness assumption (issue #2)** — is targeting `disp_bl`'s parent definitely the display
   controller? Could there be a case where two `pwm-leds` nodes both matter?
7. **`west.yml` correctness** — zmk now points at a personal fork (`honeycat`); base is
   `zmkfirmware @ 64daf698`. Any drift risk / is the fork branch reproducible?

## Pointers
- `CODE-REVIEW.md` §1–§9 — full per-change write-up, file-by-file table, risk + revert notes.
- `ZMK-3156-DEEP-DIVE.md` — the reconnect root-cause analysis + escalation rationale.
- `TOUCH-SCREEN-NOTES.md` — UI design, nav map, staging, deferred swipe-to-back.
- Revert lever for the riskiest change: repoint `west.yml` zmk to `zmkfirmware @ 64daf698`.
