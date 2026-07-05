# Prototype Mk1 — issues log

Every major issue this project hit, its root cause, and the change that fixed it. Consolidated
2026-07-06 from the earlier docs set. Current/architecture reference is the companion
`information.md`.

---

## Unsolved

Things not yet confirmed working, or deliberately not done.

### U1 — Sleep/wake reconnect: never confirmed on hardware since the fork fix (the one real open item)
The ZMK #3156 reconnect fix (see S2 below) is applied, builds green on every central target, and
its **sensor path was indirectly validated** by the dongle-encoder fix (S6). But the actual
symptom — a half deep-sleeping and waking, then still typing — has **not been explicitly
re-confirmed on hardware** since the deferred-subscribe fork landed.
- **How to verify:** flash all three parts; idle until both halves deep-sleep (~20 min), wake
  them ~together, type on each; repeat ~5–10 cycles. Healthy = both keep working with no dongle
  reset. To read the log, temporarily set `CONFIG_ZMK_USB_LOGGING=y` and watch USB CDC for
  `Position-state subscription established (peripheral N)` per half per wake (the old failure
  showed `Discover complete` with no position-state line, and at DBG a `-12`/`-ENOMEM`). Turn
  logging back **off** afterwards (it leaks touch-typed keys — see S14).
- **If it still recurs at `BT_ATT_TX_COUNT=20`:** do not raise buffers further; the escalation is
  the (already-applied) code fix's remaining hardening, or fully serialising the flush via
  completion callbacks. But the deferred-subscribe fix should make this unnecessary.

### U2 — New touch UI is hardware-untested (feel + appearance)
CI-green but never seen on the panel: the menu swap (trackpad on HOME, media in the hub), the
0–10 sensitivity levels (default 5 ≈ 4×), the flush-right scroll lane, the greyed end-stop
buttons, and especially the **180° rotation** (runtime `display_set_orientation` + touch flip +
full redraw is logically sound but unverified on glass — watch for redraw artifacts or a
touch/display mismatch after rotating). All are single-constant tweaks if the feel is off.

### U3 — 90° rotation not implemented (limitation, not a bug)
Only 180° is offered. 90°/270° would swap the panel to a 240×280 portrait geometry, which breaks
every widget position (the whole layout assumes 280×240 landscape). Doing 90° means a portrait
re-layout pass, deferred by agreement.

### U4 — brightness.c ALS path fixed but dormant
The ambient-light-sensor fade loop had real bugs (see S10); rewritten, but it is compiled out on
our build (`CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR=n`, no APDS9960 fitted), so the fix is
unverified on hardware. Only matters if a light sensor is ever added.

### U5 — Optional: upstream the #3156 fix to ZMK
A local candidate branch `pr/3156-deferred-subscribe` + `PR-3156.md` write-up exist in
`_touchref/zmk` (comments cleaned to upstream style, authored as the user). Not pushed — pending
personal review, a rebase onto current upstream main, and a check for competing PRs. Landing it
upstream would retire the zmk fork and its maintenance/rebase burden.

---

## Solved

### S1 — First pairing: "battery shows but no keys" (stale BLE bond)
**Symptom:** both halves connect and report battery, but no keypress registers. Dongle log showed
the central found + subscribed to key-position, then immediately `Security failed … level 1 err 2`
(PIN_OR_KEY_MISSING) and the subscription tore down.
**Root cause:** a Bluetooth bond-key mismatch between the XIAO and the halves. The split service
needs an encrypted link; with stale/mismatched keys, encryption never elevates and the key
subscription drops (battery still shows because that read happens at L1 first). Reset-proof
because a UF2 flash does not wipe NVS, and there was no xiao_ble settings_reset to clear the XIAO.
**Fix:** added a `reset-waveshare` (xiao_ble settings_reset) build; wipe bonds on all three
devices, then re-pair dongle → left → right. **Confirmed on hardware.**

### S2 — Reconnect after sleep: half links but sends no keys (ZMK #3156)
**Symptom:** after a half deep-sleeps and reconnects, it re-establishes the link but never
re-subscribes to key positions — no keys until the **dongle** is power-cycled. Persisted even with
`CONFIG_BT_ATT_TX_COUNT=20`.
**Root cause:** in ZMK's `central.c`, the peripheral characteristic-discovery walk subscribes to
each characteristic **inline, mid-walk**. The battery (and position-state) subscribe each start a
nested CCC auto-discovery that overlaps the in-flight walk. Under reconnect concurrency this (a)
contends for the shared ATT TX buffer pool and (b) reuses one `sub_discover_params` struct across
subscribes. When a nested discovery fails with `-ENOMEM`, Zephyr's `gatt_discover_next` reports it
to the callback as `attr == NULL` — **identical to a clean completion** — so ZMK thinks discovery
finished, never subscribes position-state, and sits CONNECTED-but-deaf (the failure masquerades as
success, so nothing retries). A central reset forces a clean sequential re-discovery, which is why
only a dongle reset cleared it.
- The buffer knob: older Zephyr fed ATT from the L2CAP pool (ZMK's `BT_L2CAP_TX_BUF_COUNT=5` gave
  centrals ~5 ATT buffers, enough that the collision never starved); Zephyr 4.1 gave ATT its own
  default-3 pool (`BT_ATT_TX_COUNT`), exposing #3156. PR #3216 bumped it to 10 for centrals; we
  set 20. That our `=20` (≫ the historical ~5) still failed proves **concurrency, not buffer
  count, is the binding constraint.**
**Fix (zmk fork `central.c`):** defer every subscription out of the walk into
`split_central_flush_subscriptions()`, issued sequentially after the walk stops (at both
walk-exit points); give sensor + battery their own `disc_params`; treat a failed or never-issued
position-state subscription as fatal for the link (disconnect → peripheral re-advertises →
discovery re-runs, converting silent-deaf into a visible self-healing reconnect); clear cached
CCC + value handles on slot release (covers reflashing a half while the dongle stays up).
Position-state is flushed first, so keys work even if battery later contends. **Applied + CI-green;
final sleep/wake hardware confirmation is U1.**

### S3 — Display brightness never changed
**Symptom:** the Settings brightness ± buttons did nothing; the panel stayed at one level.
**Root cause:** the build has **two** `pwm-leds` nodes — `disp_bl` (pwm1, P1.11, the real display
backlight) and the keyboard-backlight relay placeholder (pwm0, P0.08, an unconnected pad).
`brightness.c` bound the controller with `DEVICE_DT_GET_ONE(pwm_leds)`, ambiguous with two
instances — it resolved to the dead placeholder, so every `led_set_brightness` drove nothing.
**Fix:** bind the display controller specifically via `DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)))`.
Immune to any number of other pwm-leds nodes; identical to the old code on stock prospector (one
node). Added a `LOG_INF` so the serial log confirms the call + `led_set_brightness` return code.

### S4 — Boot-variant number-key corruption (thread safety)
**Symptom:** numpad digits occasionally send the wrong key, pattern changing boot to boot;
volume/media fine.
**Root cause:** `zmk_behavior_invoke_binding()` writes the shared keyboard HID report synchronously
on the calling thread, and ZMK's HID/endpoints path has no cross-thread lock. The fork's UI invoked
key-sends from the **LVGL display thread**, racing the dongle's normal key path → corrupted/dropped
reports. Consumer keys (volume) use a separate, less-contended report, which masked it. A first cut
used `zmk_behavior_queue_add`, but with `wait=0` (our only use) that runs **synchronously on the
caller** — not a thread hop — so the display thread was still doing the unsafe invoke.
**Fix:** a lock-free SPSC ring + `k_work` marshals every touch key/macro send onto the system
workqueue (`queue_key` → `key_work_handler`), which calls `zmk_behavior_queue_add` from inside the
handler (safe there). The display thread now only enqueues.

### S5 — Crash opening the Numpad
**Symptom:** opening the numpad hard-faults the dongle.
**Root cause:** `draw_cell` builds two LVGL objects per cell; the 4×4 numpad (~32 objects) is ~2×
any other screen and exhausted the OPERATOR layout's 20 KB LVGL pool → `lv_obj_create` returned
NULL → the next `lv_obj_set_*(NULL, …)` dereferenced it.
**Fix (three layers):** LVGL pool → 28672 (RAM is the ceiling: 32768 overflowed by ~5 KB, funded
partly by right-sizing the behavior queue 512→64 via cmake-args); `draw_cell` NULL-guards (skip
the cell, don't crash); key-sends moved to the tested queue path (S4).

### S6 — Dongle encoders stopped working
**Symptom:** the halves' encoder wheels did nothing through the waveshare dongle.
**Root cause:** the waveshare overlay had no `zmk,keymap-sensors` node, so `ZMK_KEYMAP_HAS_SENSORS`
was false in that build → the central compiled out ALL sensor handling → it never subscribed to the
peripherals' encoder (sensor) characteristic.
**Fix:** added the sensors node + two dummy EC11 encoders on unrouted XIAO P1 pins (P1.00–P1.03),
mirroring the nano dongle. The EC11 driver inits them harmlessly; the real encoder data arrives
over BLE. (Bonus: this exercised the reconnect fork's sensor-subscription path for the first time.)

### S7 — Whole-screen trackpad motion never worked
**Symptom:** the first full-screen trackpad shipped with clicks working but no pointer motion
("can't use it as a trackpad").
**Root cause:** two bugs. (1) Motion was gated on `evt->sync`, but the CST816S carries sync on the
BTN_TOUCH event, not on ABS_X/Y, so the motion branch never ran. (2) The touch-down baseline was
reset every cycle because BTN_TOUCH=1 re-reports while held and the down-logic wasn't edge-triggered.
**Fix:** sample motion on ABS events; edge-trigger down/up (`!active` / `!evt->value && active`).
Smooth scroll later proved the position stream was clean, confirming the diagnosis.

### S8 — Stray click after corner-exit
**Symptom:** tapping then corner-exiting the trackpad within the double-tap window fired a left
click ~180 ms after leaving the page.
**Root cause:** the deferred first-tap timer (`tp_tap_work`) was left scheduled on corner exit.
**Fix:** cancel + clear the pending first-tap on corner exit. (Related accepted quirk:
tap-then-immediately-drag still fires the deferred click mid-drag — the tap was an intended click;
cancelling would eat legitimate clicks.)

### S9 — Armed one-shot modifier lingering past the hub
**Symptom:** a modifier armed on the Modifiers screen, if you left the hub without sending a key,
would apply to the next key typed anywhere (armed Ctrl → a stray Ctrl+A later).
**Root cause:** `pending_mods` persisted across the transition to a non-hub view.
**Fix:** `show_view()` clears `pending_mods` on any transition to a view `< VIEW_HUB`. The intended
arm→navigate→key flow stays inside the hub, unaffected.

### S10 — brightness.c ALS fade-loop bugs (latent)
**Root cause:** the ambient-light fade loop stepped a `uint8_t` and checked it `< 0` (never true —
underflow wraps to 255) and could overshoot/oscillate at the bounds.
**Fix:** rewritten with signed maths + clamp + early-out at the end stops. Compiled out on our
build (no light sensor) — see U4.

### S11 — west.yml pin bump hit the wrong revision line
**Root cause:** a bulk `revision:` replace grabbed the first match (zmk) instead of the intended
prospector line.
**Fix (process):** always replace by exact-SHA match and assert the new SHA occurs exactly once in
the file before writing.

### S12 — Zephyr 4.1 migration build failures (historical, all resolved)
The move to ZMK v0.3.0 / Zephyr 4.1 / LVGL 9.3 hit a sequence of build failures, all fixed:
- **backlight chosen node missing** — the shield board overlay must be named after the board
  target (`nice_nano_nrf52840_zmk.overlay`), not the bare/old name, or the PWM-backlight overlay
  never applies.
- **display chosen node undeclared** — englmaxi's `dongle_display` ships only widgets; the display
  device + `chosen { zephyr,display }` must live in the dongle's own shield overlay.
- **`nice_view_spi` undefined label** — the consumer shield must be listed AFTER
  `nice_view_adapter` (which provides the label) in `build.yaml`.
- **cycle_animation event API** — Zephyr 4.1 removed `new_<event>()`; use
  `raise_<name>((struct <name>){…})`, and `BEHAVIOR_DT_INST_DEFINE` init level must be `POST_KERNEL`.
- LVGL 8→9.3 widget/asset translation (canvas layers, `LV_COLOR_FORMAT_L8`/`I1`, image header
  magic/stride) — done to match ZMK core's migrated widgets.

### S13 — Stale nav targets after the menu swap (caught in review, 2026-07-06)
**Root cause:** the media↔trackpad menu swap updated the HOME/SETTINGS handlers but left three
follow-on targets pointing at the pre-swap views: hub cell 4 → TRACKPAD (should be MEDIA), media
back → HOME (should be HUB), trackpad exit → HUB (should be HOME).
**Fix:** repointed all three to match the new tree.

### S14 — USB logging left on, leaking touch-typed keys
**Root cause:** `CONFIG_ZMK_USB_LOGGING=y` outlived touch calibration; its INF-level tap logs
(`tap … -> cell N`) reveal what's typed on the touch numpad/symbols pages to anything watching the
USB CDC port.
**Fix:** set `=n`. Re-enable **temporarily** only for the reconnect verification (U1), then turn it
back off.

---

## Audited and ruled out (not bugs)

- **#3234** (Zephyr 4.1 stopped auto-enabling `CONFIG_BT`/`ZMK_BLE`/`ZMK_SPLIT_BLE`/`ZMK_USB`) —
  our firmware demonstrably does BLE-split + USB HID; our pin post-dates the regression window.
- **`BT_MAX_CONN=5`** on the waveshare dongle — max simultaneous = 2 halves + 1 host = 3; 5 leaves
  margin. Not the reconnect cause (the user correctly rejected this guess); `BT_MAX_CONN` and
  `BT_L2CAP_TX_BUF_COUNT` do not touch the ATT pool on Zephyr 4.1.
- **Two `&i2c1` declarations** (our cst816s @0x15 + the adapter's apds9960 @0x39) — I2C is
  multi-drop; distinct addresses; the apds9960 init failure (no sensor) is per-device and doesn't
  disable the bus.
- **Input-split subscribe left inline** in the zmk fork (not deferred) — compiled out for us
  (`CONFIG_ZMK_INPUT_SPLIT` off). If ever enabled, it needs the same deferral as position-state.
- **Rapid taps** — the tap hand-off is a single atomic slot drained every 30 ms; two taps inside
  one 30 ms window would drop the earlier. Irrelevant at human tap rates (and the trackpad, where
  fast taps matter, bypasses this path).
