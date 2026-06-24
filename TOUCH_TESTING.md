# dev/touch-testing — touch drives the keyboard

Ambitious touch features where the panel acts as **soft buttons that control the
keyboard**. The XIAO + Waveshare dongle is the split **central**, so it can
inject ZMK behaviors and HID reports that reach the host and the halves.

> Higher risk than `dev/touch-easy`: every action calls into ZMK internals, and
> a real on-screen UI (LVGL buttons) is a later step. This branch ships the
> gesture+zone **pipeline** green; the action bodies are stubbed (`LOG_INF` +
> the exact ZMK API named) until verified against the ZMK 4.1 source / hardware.

## Tap-zone map (2 cols × 3 rows on the 240×280 panel)

```
 ┌───────────────┬───────────────┐
 │ 0  Layer LOWER│ 1  Layer RAISE│
 ├───────────────┼───────────────┤
 │ 2  BT prev    │ 3  BT next    │   (zone 2 long-press = clear bonds)
 │    /clear     │               │
 ├───────────────┼───────────────┤
 │ 4  Play/Pause │ 5  USB⇄BLE    │
 └───────────────┴───────────────┘
   swipe ▲ / ▼  anywhere = Volume +/-
```

Tap = momentary action, **long-press (≥600 ms)** = the alternate (e.g. clear
bonds). Zones, thresholds, and long-press time are constants in
`app/src/touch/touch_input.c`.

## Hardware wiring + architecture

Same CST816S foundation as `dev/touch-easy` (`&xiao_i2c` @ 0x15, INT→D0, RST→D1;
`CONFIG_INPUT` + `CONFIG_INPUT_CST816S`; gated by
`DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)`). The decoder additionally tracks
**touch duration** (for long-press) and the **tap coordinate** (for the zone),
then dispatches.

## Action implementation plan (the TODOs, with the ZMK 4.1 API to use)

| Zone / gesture        | Action            | ZMK call (verify signature on 4.1)                    |
|-----------------------|-------------------|--------------------------------------------------------|
| Zone 0 / 1 tap        | Toggle a layer    | `zmk_keymap_layer_toggle(LOWER/RAISE)`                 |
| Zone 2 tap            | Prev BT profile   | `zmk_ble_prof_prev()`                                  |
| Zone 2 long-press     | Clear BT bonds    | `zmk_ble_clear_bonds()` (or active-profile clear)      |
| Zone 3 tap            | Next BT profile   | `zmk_ble_prof_next()`                                  |
| Zone 4 tap            | Media play/pause  | `zmk_hid_consumer_*` + `zmk_endpoints_send_report()`   |
| Zone 5 tap            | Output USB ⇄ BLE  | `zmk_endpoints_toggle_transport()`                     |
| Swipe ▲ / ▼           | Volume up / down  | HID consumer Vol+/Vol- (press+release report pair)     |

Notes for wiring these:
- **Run on the system workqueue, not the input callback context.** The input
  callback may be in IRQ/driver context; queue a `k_work` that performs the ZMK
  call so behaviors/HID run on a thread.
- **Media/volume = HID consumer key**: send a press report then a release report
  (mirror how `behaviors/behavior_key_press.c` + `hid.c` build consumer usages).
- **Layers** need the keymap layer indices — reuse the `LOWER`/`RAISE` defines.
- The on-screen buttons can be drawn later as a prospector status-screen widget;
  dispatch keys off coordinates, so the art and the logic are independent.

## Macro pad (extension)

The 6-zone grid is the minimal on-screen pad. For a fuller macro pad, add a
"pad mode" toggled by a corner long-press, then map a denser zone grid (e.g.
3×4) to `zmk_behavior_queue_add()` / keycode sends. Document each tile here.

## Open questions (need ZMK 4.1 source or hardware)

- Exact 4.1 signatures/availability of the `zmk_ble_prof_*`,
  `zmk_endpoints_*`, and HID-consumer helpers (some are internal headers).
- `&xiao_i2c` / `&xiao_d` nodelabels on `xiao_ble//zmk` (see overlay fallback).
- Touch coordinate orientation vs. the rendered screen (may need axis flip).
