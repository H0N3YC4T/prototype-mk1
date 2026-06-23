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

## Build staging
1. **Canary (current):** build.yaml reduced to `reset` only → validates manifest +
   workflow + `nice_nano//zmk` board id with zero LVGL involvement.
2. Re-enable a keyboard target + migrate `nice_view_gem` LVGL widgets.
3. Re-enable `dongle` + migrate `dongle_display` LVGL widgets.
4. Restore full target set; pin ZMK revision.

## Hardware-verify (cannot be caught by CI — needs flashing the physical board)
- **NFC pins as GPIO:** columns use P0.09 (NFC1) and P0.10 (NFC2). Upstream
  `nice_nano` board should set `&uicr { nfct-pins-as-gpios; }`. After flashing,
  confirm the keys on those two columns register. If dead → NFC-as-GPIO regressed.
- Backlight (PWM P0.08) and WS2812 underglow still work after the board-overlay rename.

## Errors & resolutions log
_(append each CI failure + its fix here so debugging stays referenceable)_

- _(none yet — first canary push pending)_
