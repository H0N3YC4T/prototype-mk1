# Project review — state of the codebase

Reviewed 2026-07-05 at `dev/touch-screen` 91ec67a (prospector fork 3ba7e8a, zmk fork 62ba9e5),
just before the merge to `main`. This is the "visit later" document: what the system is, how the
pieces fit, what quality looks like in each area, and which rules must not be broken. For the
issue-by-issue history see CODE-REVIEW.md and REVIEW-BRIEF.md; this doc is the map, not the log.

---

## 1. What this is

A DIY split keyboard (**Prototype Mk1**) on ZMK v0.3.0 / Zephyr 4.1 / LVGL 9.3:

| Piece | Hardware | Role |
|---|---|---|
| Halves | nice!nano v2 + nice!view (gem layout) | BLE peripherals; static display frame |
| **Dongle** ("waveshare") | Seeed XIAO nRF52840 + Waveshare 1.69" LCD (280x240, glass corners R5.15mm) + CST816S touch | Split central; colour status screen + touch UI + trackpad |
| Old dongle ("nano") | nice!nano v2 | Legacy central, still buildable |

The dongle runs the OPERATOR layout from a fork of `carrefinho/prospector-zmk-module`.

## 2. Repo topology — three repos, one discipline

1. **This repo** — shield definitions, keymap, `app/src/touch/touch_input.c`, `config/west.yml`.
2. **`H0N3YC4T/prospector-zmk-module` @ `feat/new-status-screens`** (local clone `_touchref/prospector`) —
   the whole on-screen UI (`status_screen.c`), brightness, widgets.
3. **`H0N3YC4T/zmk` @ `fix/3156-deferred-subscribe`** (local clone `_touchref/zmk`) —
   split-central reconnect fix (ZMK #3156).

**The discipline:** both forks are pinned by exact SHA in `config/west.yml`. Every fork change =
commit fork → push → replace the pin (assert the SHA occurs exactly once — a bulk replace once
grabbed the wrong `revision:` line) → commit + push this repo → CI. `west.yml` has THREE
`revision:` lines; the zmk pin and prospector pin are the two fork lines. Never bump one thinking
it is the other. Fork rebase procedure: MIGRATION_NOTES.md.

## 3. Touch stack architecture

```
CST816S (I2C)  →  Zephyr input_cst816s  →  touch_input.c (this repo)
                                             │  taps: raw screen coords
                                             ▼
                                  prospector_touch_tap()   [weak → fork overrides]
                                  prospector_touchpad_active()
                                             │
                              status_screen.c (fork): views, grids, key sends
```

- `touch_input.c` owns the **panel→screen transform** (90° CW: `sx=ty`, `sy=PANEL_W-tx`) and all
  **gesture recognition** (tap / trackpad state machine). It knows nothing about views.
- `status_screen.c` owns **views and cell mapping** (`grid_rows`/`grid_cols` per view) and all
  **rendering**. It knows nothing about the sensor.
- The seam is two weak symbols, so this repo builds green with or without the fork present.

**CST816S facts that shaped everything** (re-learn these before touching gestures):
- Single-touch only — two-finger gestures are physically impossible.
- Re-reports `BTN_TOUCH=1` every cycle while held → down/up logic must be edge-triggered.
- Carries `evt->sync` on BTN_TOUCH, **not** on ABS_X/Y → motion must be sampled on ABS events.

**Threading model (the invariant):** three contexts exist — input callback (driver), LVGL display
thread (`ui_timer_cb`), system workqueue. ALL `zmk_hid_*`/`zmk_endpoint_*`/behavior invokes happen
ONLY on the system workqueue. Marshalling: atomics + `k_work_submit` from the input callback;
SPSC key-ring + `k_work_submit` from the display thread. Two real bugs came from breaking this
(display-thread key sends corrupted the HID report; `zmk_behavior_queue_add(wait=0)` is NOT a
thread hop — it runs synchronously on the caller). Details: REVIEW-BRIEF.md §0.

## 4. UI design language

- **Palette is role-based** and reuses the OPERATOR theme colours: lilac `COLOR_ACCENT` = normal
  keys, red `COLOR_BACK` = back/exit, pastel blue `COLOR_PAGE` = navigation + numpad operators +
  armed states. Hint/structure greys are named (`COLOR_HINT*`, `COLOR_LANE_*`, `COLOR_BTN_BG`).
- **Navigation is chevron-based**: red ▲ = back/up a level (all 8 back buttons), blue ▲/▼ =
  prev/next page. Menu items are LVGL built-in glyphs where one exists (audio/settings/keyboard/
  GPS-arrow-as-cursor); `Fn`, `123`, `#$%`, `MOD` stay text because no glyph exists.
- **Armed one-shot modifiers**: solid blue fill + black text on the button, plus a blue rounded
  frame (radius = glass) around the whole screen, visible from any key page.
- **Corner geometry**: the glass is R5.15mm ≈ **44px** (`GLASS_RADIUS`). The button grid draws
  inside a 5px inset (`UI_PAD`); full-screen frames use radius 44 to hug the glass. Touch zones
  still span the full screen — only drawing is inset.
- One font everywhere: `lv_font_montserrat_20` (only size compiled in).

## 5. Quality assessment by area

| Area | File | Verdict |
|---|---|---|
| Gesture driver | `app/src/touch/touch_input.c` | **Good.** Clean state machine (PENDING/MOTION/SCROLL), edge-triggered, thread-safe, tunables documented. `TP_SCROLL_ZONE_X` (240) must match the fork's rendered lane — the one cross-repo magic number. |
| Touch UI | fork `status_screen.c` | **Good.** One draw helper (`draw_cell_impl`), per-view switch, NULL-guards on every LVGL alloc (pool exhaustion policy: skip, don't crash). The key-ring comment block is the best documentation of the threading gotcha — keep it. |
| Brightness | fork `brightness.c` | **Fine.** Binds `disp_bl`'s parent specifically (two pwm-leds nodes exist; `DEVICE_DT_GET_ONE` was the ambiguity bug). Never touches the keyboard `&bl` relay. |
| Reconnect fix | zmk fork `central.c` | **Sound but upstream-shaped.** Deferred subscribes + separate sensor disc_params + self-heal disconnect. Validated on hardware including the sensor path (encoders). Candidate for upstreaming to ZMK. |
| Shield DT/conf | `prototype_mk1_waveshare.*` | **Adequate, with deliberate hacks** — see §6. LVGL pool 28672 is RAM-ceiling-tuned (32K overflowed by ~5K). |
| Build matrix | `build.yaml` | **Fine.** Dongle cmake-args right-size the behavior queue (512→64) and drop ext-power. |
| Keymap | `config/prototype_mk1.keymap` | User-maintained; not reviewed for layout logic. Builds clean on all targets. |
| Halves display | `nice_view_gem/` | **Intentional minimalism**: `NICE_VIEW_ANIMATION default n` = static frame as a setting. Full theme system preserved on `dev/periph-theme` (see §8). |

Overall: the codebase is **not confusing** if you hold two models: (1) the three-repo pin
discipline (§2), (2) the three-thread rule (§3). Everything else is a plain per-view switch or a
plain state machine, and each past bug is documented next to the code that had it.

## 6. Deliberate hacks (documented, not accidents)

- **Dummy encoders on the dongle** (`prototype_mk1_waveshare.overlay`): EC11 nodes on free XIAO
  P1 pins + a `zmk,keymap-sensors` node. The dongle has no physical encoders — the node exists
  because `ZMK_KEYMAP_HAS_SENSORS` is per-build, and without it the central compiles out ALL
  sensor handling and never subscribes to the halves' encoders (the "encoders stopped working" bug).
- **Backlight relay placeholder** (P0.08): keeps `&bl` bindings valid on the dongle; the real
  backlight lives on the halves. Settings brightness steps `disp_bl` only.
- **Trackpad hotspots are code constants, not rendered zones**: corner-exit 40px and scroll lane
  x≥240 live in `touch_input.c`; the fork only *draws* the affordances. Change one → change both.

## 7. Known quirks & accepted trade-offs

- **Tap-then-immediately-drag** fires the deferred left click (~180ms) mid-drag. Accepted: the tap
  was an intended click; cancelling would eat clicks. Revisit if misclicks annoy.
- **Left-click latency = `TP_DTAP_MS` (180ms)** — the cost of 1-tap-L / 2-tap-R. Alternative
  (long-press = right click, instant left) documented in chat, not implemented.
- **Back vs prev-page share the ▲ glyph** on paginated key screens, distinguished by red/blue only.
- **`GLASS_RADIUS 44` assumes R5.15mm is the active area** radius. If it's the module outline,
  tune by eye — single constant.
- **Peripheral battery not shown on the halves' static frame** — by design (user doesn't look).
- Idle timeout (5s) applies outside the hub only; hub/trackpad wait for explicit exit — intended.

## 8. Branch & fork map (post-cleanup, 2026-07-05)

| Ref | Purpose |
|---|---|
| `main` | The live config. Everything below merged in. |
| `zmk-0-3` | **Historical** pre-touchscreen config, keymap refreshed to current. |
| `dev/periph-theme` | **Restore point** for the full 6-theme + `cycle_animation` system. To restore: `ONLY=n`, `ANIMATION=y` (+ the hotkey-relay fix, tag `archive/dev-fix-theme`). |
| tags `archive/dev-touch-easy`, `archive/dev-touch-testing`, `archive/dev-fix-theme` | Heads of deleted branches, kept forever. |
| fork `prospector…/feat/new-status-screens` | Pinned by west.yml — do not delete. |
| fork `zmk…/fix/3156-deferred-subscribe` | Pinned by west.yml — do not delete. |

## 9. Doc index

| Doc | What it holds |
|---|---|
| PROJECT-REVIEW.md | This map. |
| TOUCH-SCREEN-NOTES.md | Touch hardware bring-up: wiring, DT, calibration, design. |
| REVIEW-BRIEF.md | Dense technical brief of the touch-era change set + threading deep-dive. |
| CODE-REVIEW.md | Issue-by-issue findings from the second-pass review. |
| IMPROVEMENT-PLAN.md | The WO-1..11 work orders (all feature WOs done; WO-5/6 = this cleanup/doc pass). |
| ZMK-3156-DEEP-DIVE.md | Root-cause analysis of the reconnect bug + the fork fix. |
| MIGRATION_NOTES.md | How to rebase the forks onto future ZMK/prospector releases. |

## 10. If you pick this up cold

1. Read §2 and §3 above. They are the two things that bite.
2. `west.yml` pins are the source of truth — the local `_touchref/` clones are working copies.
3. Firmware comes from GitHub Actions (`build.yaml` matrix); flash `dongle-waveshare` for the
   dongle, the left/right images for the halves.
4. Tuning knobs live at the top of `touch_input.c` (gestures) and in the `#define` block at the
   top of the fork's `status_screen.c` (visuals).
