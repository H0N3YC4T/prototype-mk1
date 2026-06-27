# ZMK → Zephyr 4.1 Migration Notes

Working branch: `upgrade/zeph-4-1`. Scratchpad for the migration — error→fix log,
decisions, and hardware-verify items. Remove (or move to `docs/`) before final merge.

## CURRENT STATUS (supersedes the in-progress notes further below)
Migration **complete**: all **6** targets green on ZMK main / Zephyr 4.1 / LVGL
9.3 — `reset`, `dongle` (nice!nano + nice!view), `dongle-waveshare` (XIAO +
prospector), `central-left`, `left`, `right`. Displays modularised; the gem theme
system + cycle_animation hotkey are built; `deferred-features/` and the
upstream-ref clones are removed. Pins: zmk `64daf698`, zmk-dongle-display
`2bb333f8`; nice-view-gem is vendored locally; **prospector still tracks the
`feat/new-status-screens` branch, not a commit** (reproducibility risk — pin it).

**Waveshare dongle "battery shows but no keys" — SOLVED (stale BLE bond).**
Both halves connected and reported battery, but no keypress registered. The
dongle serial log showed the central DID find + subscribe to the key-position
characteristic, then immediately `Security failed ... level 1 err 2`
(BT_SECURITY_ERR_PIN_OR_KEY_MISSING) and the subscription was torn down. Root
cause: a Bluetooth bond-key MISMATCH between the XIAO and the halves — the split
service needs an encrypted (L2) link, and with stale/mismatched keys encryption
never elevates so the key subscription drops (battery still shows because that
read happens at L1 first). Reset-PROOF because a UF2 flash does NOT wipe the
settings/NVS partition, so the XIAO kept stale bonds from earlier attempts and
there was no xiao_ble settings_reset to clear them. **Fix:** added a
`reset-waveshare` (xiao_ble settings_reset) build; wipe bonds on ALL THREE
devices, then re-pair dongle → left → right. CONFIRMED working on hardware.
NB: ZMK #3156 / battery-fetch / BT_ATT_TX_COUNT was a RED HERRING (reverted).

**TEMP (revert when done):** gem locked to the transmutation theme at ~15 fps —
`CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY=y` (drops the other 5 themes' bitmaps
from the peripheral build) + a transmutation lock in `nice_view_theme_set()` +
`frame_ms 33→66`. Touch-screen work is scaffolded on `dev/touch-easy` /
`dev/touch-testing` (CST816S input pipeline; actions stubbed).

**Done since:** removed ZMK Studio (was dead flash), the unbound `bks_del`, and
all deprecated `label=` props; settings layer trimmed to 3 BT profiles. **Still
open:** pin prospector to a commit; revert the TEMP transmutation-only gem when
other themes are wanted again.

## Target
- **ZMK:** `zmkfirmware/zmk` `revision: main` (Zephyr **4.1**, LVGL **9.3.0**).
  Tracking `main` during migration; **pin to the green commit at the end**.
- Authoritative checklist: ZMK blog "Zephyr 4.1 Update" (2025-12-09).
- The XIAO/Waveshare dongle (secondary) uses `carrefinho/prospector-zmk-module`
  `feat/new-status-screens` (OPERATOR theme), which also targets ZMK `main`/Zephyr 4.1.

## Future features (planned, not yet implemented)
Do these on a dedicated `waveshare-custom` branch, AFTER the XIAO dongle is
hardware-verified (display lights up + shows the OPERATOR screen).

### XIAO/Waveshare dongle — display brightness via keypress
- Prospector drives the display backlight as a standard Zephyr PWM-LED: `disp_bl`
  (pwm1, P1.11), set via `led_set_brightness(pwm_leds, DISP_BL, v)`. Only fixed/
  sensor modes ship — no key binding.
- DO NOT reuse ZMK's backlight (`&bl`): that channel is already the keyboard
  lighting, and ZMK backlight state SYNCS across the split — so it would weld the
  display brightness to the keyboard glow (one key changes both, relayed).
- Instead: a DEDICATED behavior (e.g. `&disp_bri UP/DN`) that targets `disp_bl`
  by node label and calls `led_set_brightness`. Guard with
  `DT_NODE_EXISTS(DT_NODELABEL(disp_bl))` so it's a no-op on the nice!nano builds
  (safe to keep in the shared keymap). Add settings persistence if wanted.

### XIAO/Waveshare dongle — touch (CST816S) support
- Decided wiring (no ambient-light sensor): touch SDA→D4, SCL→D5 (shared `i2c1`),
  IRQ→D0, RST→D1. (Display uses D3/D6/D7/D8/D9/D10; D2 left free.)
- Firmware: add a `hynitron,cst816s` node on `&i2c1` (addr 0x15) with
  `irq-gpios = <&xiao_d 0 ...>`, `reset-gpios = <&xiao_d 1 ...>`, plus ZMK input
  plumbing. Not wired in firmware yet.

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

## Waveshare/XIAO prospector dongle (added on upgrade/zeph-4-1)
Second dongle option: XIAO nRF52840 + Waveshare 1.69" via carrefinho/prospector
(feat/new-status-screens), OPERATOR theme. Implemented as a shared-base refactor:
- `prototype_mk1_dongle_base.dtsi` — common dongle DT (mock kscan + transform +
  central chosen). Included by BOTH dongle shields.
- `prototype_mk1_dongle.overlay` (nice!view) = base + nice_view display + encoders.
- `prototype_mk1_waveshare.overlay` (XIAO) = base only; prospector_adapter provides
  the ST7789V display (240x280 = Waveshare 1.69"), PWM backlight, sensor, themes.
- New shield registered: Kconfig.shield + Kconfig.defconfig (central/split/BT block,
  deliberately NOT in the nice!view mono-LVGL block) + prototype_mk1.yml sibling +
  prototype_mk1_waveshare.conf (OPERATOR theme; fixed brightness, sensor OFF — the
  likely cause of the v0.3 "backlight didn't work").
- west.yml: carrefinho/prospector-zmk-module (revision feat/new-status-screens, WIP;
  tracked not pinned). build.yaml: `xiao_ble//zmk` + `prototype_mk1_waveshare
  prospector_adapter`, cmake-args `-DCONFIG_ZMK_BACKLIGHT=n -DCONFIG_ZMK_EXT_POWER=n`
  (globals from config/prototype_mk1.conf target the nice!nano halves; XIAO lacks
  that hardware → would assert).
- Watch: prospector warns of RAM overflow on XIAO → if linker `region RAM overflowed`,
  add `CONFIG_LV_Z_VDB_SIZE=25` to prototype_mk1_waveshare.conf.

## ZMK 4.1 event API change (cycle_animation behavior)
The dormant behavior used the old `new_<event>()` + `ZMK_EVENT_RAISE(*evt)` pattern.
ZMK 4.1 removed `new_<name>()`; `ZMK_EVENT_DECLARE` now generates
`int raise_<name>(struct <name>)` and builds the event header internally. Fix:
`raise_cycle_animation_state_changed((struct cycle_animation_state_changed){.type=...})`.
The event header/impl (ZMK_EVENT_DECLARE/IMPL) and the `as_<name>()` listener side
were already 4.1-correct; behavior parameter metadata also compiled fine.
Second behavior fix: `BEHAVIOR_DT_INST_DEFINE` init level must be `POST_KERNEL`,
NOT `APPLICATION` — Zephyr 4.1's `Z_DEVICE_CHECK_INIT_LEVEL` only accepts
PRE_KERNEL_1/2 or POST_KERNEL for devices, so APPLICATION hits a
`ZERO_OR_COMPILE_ERROR`. (All upstream ZMK behaviors use POST_KERNEL.)

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

## Status: CORE MIGRATION COMPLETE
All five targets green on ZMK main / Zephyr 4.1 / LVGL 9.3: `reset`, `dongle`,
`left`, `right`, `central-left` (1f965a7). Revisions then pinned for reproducibility:
- zmk            → `64daf698e073e37b6748ac54f4eb48d8666af0b9`
- nice-view-gem  → `0a50fe209d929c916c5664c6affd0f4977c6b012`
- zmk-dongle-display → `2bb333f87136d33e94a49d86236ed9ec254a8060`

### Remaining / next phases
- Optional cleanup of leftover non-fatal warnings: vestigial SSD1306/I2C forcing
  + `exposes: i2c_oled` in the prototype_mk1 shield; deprecated `label` on
  behaviors/macros/keymap nodes.
- Hardware-flash verification (CI can't catch): NFC-pin columns P0.09/P0.10;
  backlight + underglow; nice!view orientation (gem rotate 90 vs 270); dongle
  widget fit on 160x68 (englmaxi is OLED-oriented).
- Secondary scope: XIAO + Waveshare prospector dongle; controller-option
  architecture (dongle/left-as-central, halves-as-peripheral via role cmake-arg).
- Rebuild the deferred gem theme system (deferred-features/) + wire cycle_animation.
- README; eventually merge upgrade/zeph-4-1 → main.

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
