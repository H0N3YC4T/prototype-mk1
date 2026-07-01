# dev/touch-screen — 3 touch macro keys replacing the WPM / layer-name area

Goal: on the waveshare dongle, replace the **WPM area and the layer-name text** with
**3 touch macro keys** (keep the layer *indicator* dots). The dongle is the split central,
so a touch can fire a ZMK macro that reaches the host as normal HID.

## Key finding (resolves the "WPM + layer text" framing)

In the prospector OPERATOR theme:
- **`wpm_meter.c`** draws the WPM bars + WPM number **and the layer-name text** (`layer_label`
  via `zmk_keymap_layer_name`, lines 200–209). So the "WPM area" and the "layer name text" are
  the **same widget**.
- **`layer_display.c`** draws **only the dots** (the indicator you want to keep).

So "replace the WPM area and the layer text" = **replace the single `wpm_meter` widget**
(260×90 at screen pos 10,42) with 3 touch keys, and leave `layer_display` (dots) alone.

## Architecture

```
CST816S (I2C) --Zephyr input--> app/src/touch/touch_input.c
   INPUT_ABS_X/Y + INPUT_BTN_TOUCH        |  tap -> which of 3 zones
                                          v
                          zmk_behavior_invoke_binding("touch_macro_N")
                                          |  (run on system workqueue)
                                          v
                    macro -> HID report -> host (dongle is central)
```

## What's committed on this branch (the buildable foundation)

- `app/src/touch/touch_input.c` — full tap decoder + 3-zone hit test + fires `touch_macro_0/1/2`
  via the verified ZMK 4.1 API (`zmk_behavior_invoke_binding`, structs in `zmk/behavior.h`), on a
  `k_work` (not the input callback). **Whole file is gated on
  `DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)`** → with no node yet it compiles to nothing, so
  the build stays green.
- `CMakeLists.txt` — compiles the above.

Nothing is active until the steps below are done. The touch input code is ready; the blockers are
**hardware wiring** and a couple of **design decisions**, plus the **fork** display change.

## Activation — steps + DECISIONS NEEDED

### 1. Wiring (BLOCKER — needs your hardware reality)
The earlier scaffolding assumed CST816S INT→D0, RST→D1, SDA/SCL on the XIAO I2C. Two issues to
resolve before declaring the node:
- **D0 is already taken** by the backlight-relay placeholder PWM (`prototype_mk1_waveshare.overlay`,
  `PWM_OUT0 on P0.02 / D0`). So the touch INT can't also be D0 — either move the touch INT to a
  free pin, or move the placeholder backlight to a free pin (it's arbitrary; any free GPIO works).
- The prospector display already uses **`&i2c1`** (with `apds9960@39`). The CST816S (`@0x15`) can
  share that bus. Confirm the touch SDA/SCL are on the same pins as `&i2c1`.
- **Q: which XIAO pins did you actually wire the CST816S INT/RST to, and is it even populated?**

### 2. Declare the CST816S node (in `prototype_mk1_waveshare.overlay`)
Once the pins are known (here using `&i2c1`, INT=`<free pin>`, RST=`<free pin>`):
```dts
&i2c1 {
    status = "okay";
    cst816s: cst816s@15 {
        compatible = "hynitron,cst816s";
        reg = <0x15>;
        irq-gpios = <&xiao_d N (GPIO_ACTIVE_LOW)>;   /* N = the INT pin */
        rst-gpios = <&xiao_d M (GPIO_ACTIVE_LOW)>;   /* M = the RST pin */
    };
};
```
and in `prototype_mk1_waveshare.conf`: `CONFIG_INPUT=y` and `CONFIG_INPUT_CST816S=y`.
Declaring the node flips `DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)` on and activates
`touch_input.c`.

### 3. Define the 3 macros (DECISION — what should each key do?)
Add to `prototype_mk1_waveshare.overlay` (placeholders shown — change the `bindings`):
```dts
/ {
    macros {
        touch_macro_0: touch_macro_0 { compatible = "zmk,behavior-macro"; #binding-cells = <0>;
            bindings = <&kp LC(C)>; };   /* e.g. Copy */
        touch_macro_1: touch_macro_1 { compatible = "zmk,behavior-macro"; #binding-cells = <0>;
            bindings = <&kp LC(V)>; };   /* e.g. Paste */
        touch_macro_2: touch_macro_2 { compatible = "zmk,behavior-macro"; #binding-cells = <0>;
            bindings = <&kp LG(L)>; };   /* e.g. Lock */
    };
};
```
(Needs `CONFIG_ZMK_BEHAVIOR_MACRO=y` if not already pulled in.) **Q: what 3 macros do you want?**

### 4. Calibrate the zones (hardware)
Touch-panel coordinates are likely rotated vs the rendered screen. `touch_input.c` logs every tap
(`tap x=… y=… -> zone …`) over USB CDC. Flash, watch the log, tap where the 3 keys should be, and
tune `ZONE_Y_MIN/MAX` and the X split (and add an axis flip if needed). Until then the zone bounds
are a guess.

### 5. (Fork) Draw the 3 buttons in place of the WPM widget
This is in the prospector fork (same flow as the theme colours: edit fork → push → bump
`config/west.yml`). Two options:
- **Minimal:** in `…/operator/status_screen.c`, drop `zmk_widget_wpm_meter_*` and add a small new
  `touch_keys` widget at pos (10,42) size 260×90 drawing 3 labelled buttons (3 columns). Keep
  `layer_display` (dots) untouched.
- **In place:** gut `…/operator/wpm_meter.c` to render 3 buttons instead of bars/WPM/layer-label.
The button art and the touch logic are independent (dispatch is by coordinate), so the art can land
after the input is calibrated.

## Decisions blocking completion
1. Is the CST816S actually wired on this dongle, and to which INT/RST pins? (+ resolve the D0 clash)
2. What should the 3 macros do?
3. OK to spend a prospector-fork round-trip on the button art (step 5)?

## Status
- [x] Architecture + the WPM/layer-text finding
- [x] Touch decoder + 3-zone + macro-invoke (committed, gated, build-green)
- [ ] CST816S node (needs wiring decision) + input config
- [ ] 3 macro definitions (needs decision)
- [ ] Zone calibration (hardware)
- [ ] Fork: 3-button art replacing the wpm_meter widget

## REDESIGN — two-screen model (current direction)

Instead of replacing the WPM/layer-text widget, keep the main OPERATOR screen intact and add a
**second "macro screen"**:
- Tap **anywhere** on the main screen -> switch to the macro screen.
- Macro screen shows the 3 macro buttons (Vol-/Mute/Vol+) + a **Back** button.
- **Timeout** (no touch for ~N s) also returns to the main screen.

### Split of responsibilities
- **Our repo (`app/src/touch/touch_input.c`)** — done, non-regressing: detects a tap, maps to
  screen coords, and calls a weak hook `bool prospector_touch_tap(int32_t sx, int32_t sy)`. If the
  fork implements it (returns true), the fork owns the reaction; otherwise we fall back to the
  current "fire a macro by zone in the WPM strip" behaviour. So the touch keeps working today, and
  the fork can take over cleanly.
- **Prospector fork** — the actual UI. Implements `prospector_touch_tap` + the two screens.

### Fork implementation plan (`.../operator/status_screen.c`, or a new file it compiles)
1. In `zmk_display_status_screen()`, after building the main widgets, create a **macro view**:
   a full-screen `lv_obj` child (or a separate `lv_obj` screen), `LV_OBJ_FLAG_HIDDEN` by default,
   containing 3 labelled buttons + a Back button. Record their rects.
2. Implement `bool prospector_touch_tap(int32_t sx, int32_t sy)`:
   - Main view (macro hidden): show macro view, (re)start the timeout `lv_timer`. return true.
   - Macro view: hit-test the 4 buttons -> fire `touch_macro_N` via
     `zmk_behavior_invoke_binding("touch_macro_N", ...)`, or Back -> hide macro view; reset the
     timeout. return true.
3. **Timeout**: an `lv_timer` (period = the timeout) whose callback hides the macro view; reset it
   on each tap; pause it on the main view.
4. **Thread safety (important):** `prospector_touch_tap` is called from a ZMK system-workqueue
   thread, NOT the LVGL/display thread. Do NOT touch LVGL directly there. Marshal to the display
   thread -- record the tap in a static + submit to ZMK's display work queue (or use the same
   mechanism the widgets use to update), and do all `lv_obj_*` / `lv_timer_*` work in that context.
   Getting this wrong => heap/LVGL corruption.
5. Ship as a fork commit; bump `config/west.yml` to the new fork SHA (same flow as the theme
   colours), then CI.

Because it can't be hardware-tested from here (LVGL + the panel), expect a calibration/iteration
pass like the rotation fix.

---

## Two-screen nav + MACRO hub (implemented state)

Navigation map (2x3 grid, cells 0..5; cell = row*3 + col):
```
NORMAL --tap anywhere--> HOME
HOME:   top row (0,1,2) = BACK->NORMAL   3=MEDIA   4=SETTINGS   5=MACRO hub
MEDIA:  0 vol-  1 back  2 vol+   3 prev   4 play/pause   5 next
SETTINGS: 0 bright-  1 back  2 bright+   3 vol-   4 mute   5 vol+
HUB:    0 F-keys  1 back  2 numpad(123)  3 symbols(#)  5 modifiers(MOD)
```
- **Colours:** buttons = 80% of cell, radius 12, black fill; border+text = WPM-purple
  (`DISPLAY_COLOR_WPM_TEXT`); Back = low-battery red (`DISPLAY_COLOR_BATTERY_LOW_FILL`);
  page nav = pastel battery-blue (`DISPLAY_COLOR_BATTERY_FILL`). Font `lv_font_montserrat_20`
  (has the `LV_SYMBOL_*` glyphs).
- **Brightness** (settings 0/2) steps `disp_bl` only via `prospector_brightness_step()` in
  `src/brightness.c` — the display backlight, NOT the keyboard `&bl` relay (separately wired).
- **Key sends** (F-keys / numbers / symbols) go through ZMK's `&kp` behaviour:
  `zmk_behavior_invoke_binding({.behavior_dev="key_press", .param1 = keycode | (pending_mods<<24)})`.
  Keycodes come from `<dt-bindings/zmk/keys.h>` (F1.., N0.., EXCL..); mods from
  `<dt-bindings/zmk/modifiers.h>` (bits 24+). No per-key macros needed.
- **Modifiers (planned):** one-shot — a tapped mod sets `pending_mods`, applied to the *next key
  sent* (not the next tap), so navigating pages to reach a key does NOT consume it.
- **Pagination:** 4 keys/page in cells 0,2,3,5. cell 1 = Back(page 0) / Prev-page(page>=1);
  cell 4 = Next-page (blue `LV_SYMBOL_DOWN`), cyclic. Hub area (views >= VIEW_HUB) has **no idle
  timeout** so you can sit on a key page.

### Staging
- **A (done):** hub + paginated **F-keys** (F1-F12, 3 pages), key-send + one-shot-mod plumbing,
  timeout-off in hub. Uses the existing cell-based `prospector_touch_tap(int cell)`.
- **B (next):** **Numpad** = a 4x3 grid (12 cells). Needs the touch hook to pass **raw coords**
  (`prospector_touch_tap(sx, sy)`) so the UI can map cells per the *current* screen's grid
  (2x3 vs 4x3). Calculator layout: 7 8 9 / 4 5 6 / 1 2 3 / (back) 0 (enter).
- **C:** **Symbols** (paginated, `EXCL` etc. — already carry shift) + **Modifiers** screen
  (Ctrl/Shift/Alt/Gui set `pending_mods`, no pagination).

### TODO / wanted (deferred)
- **Swipe-to-back gesture:** top-to-bottom swipe = Back/exit, especially for paginated pages >= 2
  where cell 1 is Prev-page (so there's no cell Back). Needs swipe detection added to
  `app/src/touch/touch_input.c` (currently tap-only). Deferred per user; for now exit a deep page
  by Prev-page'ing back to page 1 then Back.
