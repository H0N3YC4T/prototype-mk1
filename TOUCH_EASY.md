# dev/touch-easy — display-local touch features

Touch gestures on the XIAO + Waveshare 1.69" prospector dongle that only affect
**this dongle's own screen** (no split relay, no HID). These are the lowest-risk
touch features because they never have to reach the keyboard halves.

| Gesture        | Action                                   |
|----------------|------------------------------------------|
| Swipe ◀ / ▶    | Previous / next prospector status screen |
| Swipe ▲ / ▼    | Display brightness up / down             |
| Tap            | Pause / advance the artwork theme        |

## Hardware wiring (CST816S)

| Touch pin | XIAO pin | DT reference            |
|-----------|----------|-------------------------|
| SDA       | D4       | `&xiao_i2c` (HW I2C SDA)|
| SCL       | D5       | `&xiao_i2c` (HW I2C SCL)|
| INT       | D0       | `&xiao_d 0` (active-low)|
| RST       | D1       | `&xiao_d 1` (active-low)|

I2C address `0x15`. Defined in `prototype_mk1_waveshare.overlay`; driver enabled
in `prototype_mk1_waveshare.conf` (`CONFIG_INPUT=y` + `CONFIG_INPUT_CST816S=y`).

## Architecture

```
input_cst816s (Zephyr driver, I2C 0x15, INT on D0)
        │  INPUT_ABS_X / INPUT_ABS_Y / INPUT_BTN_TOUCH
        ▼
app/src/touch/touch_input.c   ← INPUT_CALLBACK_DEFINE
   touch-down → record start (x,y)
   touch-up   → classify(Δx,Δy) → tap | swipe L/R/U/D
        ▼
   touch_dispatch()   ← the per-gesture actions (currently LOG_INF + TODO)
```

The decoder (down→up delta, tap vs. dominant-axis swipe) is complete. Compiled
only when the `cst816s` DT node exists, via
`DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)` — inert on every other target.

## Status

- ✅ **Done:** touch DT, input driver config, gesture pipeline, per-gesture
  logging. **Flash this first** and watch RTT/USB — each gesture prints a line.
  Confirm the controller responds and tune `TOUCH_TAP_MAX_TRAVEL` /
  `TOUCH_SWIPE_MIN_TRAVEL` to the 240×280 panel.
- ⏳ **TODO:** wire the three action bodies (below). They need the prospector
  module's internals, which aren't vendored here (CI-only), so they're stubbed
  until we can read that source or iterate on hardware.

## Action implementation plan

1. **Status-screen paging (swipe ◀/▶).** prospector renders multiple status
   screens; find its "set/advance active screen" entry point in
   `carrefinho/prospector-zmk-module` (the status-screen widget) and call it
   from `touch_dispatch()`. Likely a small index + a redraw.
2. **Brightness (swipe ▲/▼).** prospector already owns the backlight
   (`CONFIG_PROSPECTOR_FIXED_BRIGHTNESS`). Look for a runtime brightness setter;
   if none, drive the ST7789 backlight PWM channel directly. Step ±10%, clamp
   10–100%. Pairs with the dedicated brightness behavior in `MIGRATION_NOTES.md`.
3. **Theme pause/next (tap).** The gem artwork lives on the **peripheral** nice!
   views, not this dongle. Because the dongle is the split **central**, a tap
   must relay the existing `cycle_animation` behavior (GLOBAL locality) to the
   peripherals — invoke the behavior binding rather than raising the event
   locally, so ZMK's split relay carries it. Reuses `NVC_PAUSE` / `NVC_NEXT`.

## Open questions (need prospector source or hardware)

- Are `&xiao_i2c` / `&xiao_d` the right nodelabels on `xiao_ble//zmk`? If CI says
  undefined, swap to `&i2c0` + `&gpio0` with raw pins (D0=P0.02, D1=P0.03).
- Does `prospector_adapter` already claim `&xiao_i2c` (ambient sensor)? It's
  disabled here, but watch for a bus conflict.
- prospector's public API surface for screen index + brightness.
