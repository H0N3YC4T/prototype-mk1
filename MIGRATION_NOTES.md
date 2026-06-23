# ZMK → Zephyr 4.1 Migration Notes

Working branch: `upgrade/zeph-4-1`. Scratchpad for the migration — error→fix log,
decisions, and hardware-verify items. Remove (or move to `docs/`) before final merge.

## Target
- **ZMK:** `zmkfirmware/zmk` `revision: main` (Zephyr **4.1**, LVGL **9.3.0**).
  Tracking `main` during migration; **pin to the green commit at the end**.
- Authoritative checklist: ZMK blog "Zephyr 4.1 Update" (2025-12-09).
- The XIAO/Waveshare dongle (secondary) uses `carrefinho/prospector-zmk-module`
  `feat/new-status-screens` (OPERATOR theme), which also targets ZMK `main`/Zephyr 4.1.

## Checklist applicability (this project = upstream nice!nano v2 + shields)
| Item | Status |
|---|---|
| west.yml `v0.3-branch` → `main` | DONE |
| workflow `build-user-config.yml@v0.3` → `@main` | DONE |
| board `nice_nano_v2` → `nice_nano//zmk` | DONE (build.yaml) |
| shield board overlay `nice_nano_v2.overlay` → `nice_nano.overlay` | DONE (verify on first keyboard build) |
| LVGL 8 → 9.3.0 widget rewrite (nice_view_gem + dongle_display) | TODO — the bulk |
| HWMv2 board migration / RP2040 / DC-DC / bootloader / Cirque trackpad | N/A |
| LED strip `CONFIG_WS2812_STRIP` removal | N/A (not set; DT-driver auto-enables) |
| "features no longer auto-enabled" regression (zmk#3234) | WATCH build warnings |

## Display strategy (decided: HYBRID)
- `dongle_display`: stock copy of `englmaxi/zmk-dongle-display` → drop vendored copy,
  add as west.yml module (LVGL-9 `main`), reference shield in build.yaml.
- `nice_view_gem`: copy of `M165437/nice-view-gem` with **custom images only** →
  rebase onto M165437 LVGL-9 `main`, re-apply the custom image assets on top.
- Verify the "just images" claim by diffing vendored vs upstream BEFORE replacing
  (don't lose any customization). Preserve the dormant cycle_animation hook plan.

## Reference configs (validated patterns from migrated/real configs)
- `englmaxi/zmk-config` (ZMK main / 4.1): board ids `nice_nano//zmk` + `xiao_ble//zmk`;
  west.yml modules at `revision: main`; lists `<dongle_shield> dongle_display`
  (keyboard FIRST) — works for them because their dongle OLED is on a board-level
  i2c label (`xiao_i2c`), available from overlay start. Our nice!view uses the
  `nice_view_adapter` SHIELD (provides `&nice_view_spi`), so the consumer shield
  must come AFTER it → our reorder is correct for the nice!view case.
- `DarrenVictoriano/zmk-config` (v0.3, same architecture as ours): nice!nano v2 +
  nice!view_gem + XIAO dongle. Order `corne_left nice_view_adapter nice_view_gem`;
  nice-view-gem pulled as a west module (M165437, `v0.3.0` on v0.3 / `main` on 4.1).
  Architecture: XIAO dongle = central, BOTH halves = peripherals via cmake-arg
  `-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=n` on one shield (cleaner than separate
  `_periph_left/_right` shields) — adopt for the "dongle/left-as-controller" goal.
- `mctechnology17/zmk-dongle-display-view`: nice!VIEW-native dongle widgets
  (160x68 horizontal). Better long-term fit for our nice!view dongle than englmaxi's
  OLED(128x64)-oriented module. Future option (we kept the englmaxi-based copy for now).

## Build staging
1. **Canary (current):** build.yaml reduced to `reset` only → validates manifest +
   workflow + `nice_nano//zmk` board id with zero LVGL involvement.
2. Re-enable a keyboard target + migrate `nice_view_gem` LVGL widgets.
3. Re-enable `dongle` + migrate `dongle_display` LVGL widgets.
4. Restore full target set; pin ZMK revision.

## LVGL 8 → 9.3 translation pattern (from ZMK core nice_view on `main`)
Reference: `zmkfirmware/zmk` `app/boards/shields/nice_view/widgets/util.c`.
- Core adds local wrappers so widgets change minimally: `lv_canvas_draw_rect/line/text/img(...)` → `canvas_draw_rect/line/text/img(...)` (wrappers do `lv_canvas_init_layer` → `lv_draw_*` on the layer → `lv_canvas_finish_layer`).
- `rotate_canvas(canvas, cbuf)` → `rotate_canvas(canvas)`; reads `lv_canvas_get_draw_buf(canvas)->data` (uint8_t*), copies, `lv_draw_sw_rotate(..., LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT)`. Gem rotates 90° (flip→270°) — map to `LV_DISPLAY_ROTATION_90`/`_270`; absolute orientation is a hardware-verify item.
- Canvas color format: `LV_IMG_CF_TRUE_COLOR` → **`LV_COLOR_FORMAT_L8`**; canvas buffers become `uint8_t[CANVAS_BUF_SIZE]` (not `lv_color_t[]`); `lv_canvas_set_buffer(..., LV_COLOR_FORMAT_L8)`.
- Draw descriptors: `lv_draw_img_dsc_t`/`_init` → `lv_draw_image_dsc_t`/`_init`; set `.src` on the dsc; line dsc uses `.p1/.p2`; arc dsc uses `.center/.radius/.start_angle/.end_angle`.
- Image assets (`images.c`, `crystal.c`, `bongo_cat_images.c`, `animations/*`): `lv_img_dsc_t` → `lv_image_dsc_t`; `.header.cf = LV_IMG_CF_INDEXED_1BIT` → `LV_COLOR_FORMAT_I1`; drop `.header.always_zero/.reserved`; add `.header.magic = LV_IMAGE_HEADER_MAGIC` + `.header.stride`. (Match ZMK core's migrated `images.c` exactly.)

## Hardware-verify (cannot be caught by CI — needs flashing the physical board)
- **NFC pins as GPIO:** columns use P0.09 (NFC1) and P0.10 (NFC2). Upstream
  `nice_nano` board should set `&uicr { nfct-pins-as-gpios; }`. After flashing,
  confirm the keys on those two columns register. If dead → NFC-as-GPIO regressed.
- Backlight (PWM P0.08) and WS2812 underglow still work after the board-overlay rename.

## Errors & resolutions log
_(append each CI failure + its fix here so debugging stays referenceable)_

- **cd13044 — canary GREEN.** `reset` built against ZMK main/Zephyr 4.1.
  Confirms: `revision: main` resolves + fetches Zephyr 4.1; `@main` workflow runs;
  board id `nice_nano//zmk` is correct. Plumbing migration validated.
- **3af04c9 — dongle FAILED** at `src/backlight.c`:
  `"CONFIG_ZMK_BACKLIGHT is enabled but no zmk,backlight chosen node found"`.
  Root cause: HWMv2 board-overlay naming. The shield board overlay must be named
  after the board *target*, which for nice!nano v2 on Zephyr 4.1 is
  **`nice_nano_nrf52840_zmk.overlay`** (as ZMK core's nice_view_adapter does), NOT
  `nice_nano.overlay` (bare name doesn't match) and NOT `nice_nano_v2.overlay`
  (old pre-HWMv2 name). My earlier rename to `nice_nano.overlay` meant the
  PWM-backlight + WS2812-underglow overlay never applied → no `zmk,backlight`
  node. NOTE: the correct-named file is the one the cleanup pass deleted as a
  "byte-identical duplicate" — it existed to cover the HWMv2 board name.
  Fix: rename board overlay → `nice_nano_nrf52840_zmk.overlay`. (Also clears the
  `drivers__pwm` "No SOURCES" warning — same missing &pwm0 node.)
  Non-fatal warnings seen (deferred): vestigial SSD1306; `label` deprecated on
  behaviors/macros/keymap; KSCAN debounce unsatisfied-dep on the mock-kscan dongle.
- **0770952 — dongle FAILED** at `src/display/main.c`:
  `'__device_dts_ord_DT_CHOSEN_zephyr_display_ORD' undeclared` (no resolvable
  `chosen { zephyr,display }`). Root cause: englmaxi's `dongle_display.overlay`
  is empty by design — the module ships ONLY the status-screen widgets and
  expects the dongle's own shield to define the display device. The vendored
  copy we removed had carried the nice!view (LS0xx) node + chosen; relocating it
  to `prototype_mk1_dongle.overlay` (its proper home) on the nice_view_adapter
  `&nice_view_spi` bus, 160x68. (So the dongle wasn't fully stock — englmaxi is
  OLED-oriented; widget fit on the 160x68 nice!view is a hardware-verify item.)
  CONFIRMED by user: all THREE displays (both halves + dongle) are nice!views
  (Sharp LS0xx 160x68); no OLEDs anywhere.
- **b4b5301 — dongle FAILED** (DT parse): `undefined node label 'nice_view_spi'`
  at prototype_mk1_dongle.overlay. Overlay ordering: `&nice_view_spi` is provided
  by the nice_view_adapter shield, but prototype_mk1_dongle was listed BEFORE it,
  so the label wasn't defined when referenced. The gem works because nice_view_gem
  (the label consumer) is listed AFTER the adapter. Fix: reorder the dongle build
  shields to `nice_view_adapter prototype_mk1_dongle dongle_display`. (config/
  prototype_mk1.conf still merges last, so Kconfig precedence is unaffected.)
- **bc68592 — dongle GREEN.** Dongle target fully migrated to Zephyr 4.1 / LVGL 9:
  englmaxi dongle_display module + display node in prototype_mk1_dongle.overlay +
  shield reorder. Build sequence of fixes: backlight node → overlay HWMv2 name →
  display node location → shield order.
- **Gem modularization** (this commit): vendored nice_view_gem → M165437 module
  (LVGL-9 main); custom theme/artwork preserved to deferred-features/; periph_left
  + periph_right targets re-enabled; periph/central confs switched off the custom
  NICE_VIEW_ANIMATION* names (stock uses NICE_VIEW_GEM_ANIMATION, default on);
  removed vestigial `zephyr,display = &oled` from prototype_mk1.dtsi (gem sets
  &nice_view). Periph order `..periph_left nice_view_adapter nice_view_gem` needs
  NO reorder — the gem (label consumer) is already last, after the adapter.
