# Project review & forward work orders

Planned by Fable 5 (2026-07-03) for execution by Opus 4.8. Every design decision is
already made here — the executor implements, verifies, and documents; it does not
re-litigate choices or invent alternatives. If an instruction conflicts with
reality (file moved, SHA stale, CI red), STOP and report rather than improvise.

**Binding rules:** `REVIEW-BRIEF.md` §9.1 (operational rules — all eleven apply to
every work order). One work order per commit cycle (fork commit → pin bump → CI
green → docs), never batched. Any CI red: fix or revert before starting the next WO.

**Sequencing gate:** WO-1/2/3/8/9/10/11 change the dongle build and MUST wait until
the user's hardware round (§9.2 of REVIEW-BRIEF.md) passes on the current state
(`dev/touch-screen` @ `a5f038b`, zmk fork `c2ca24f`, prospector fork `62ba9e5`).
Layering new changes before that verification would muddy the test. WO-4/5/6/7 are
docs/hygiene and may run anytime.

**Execution order after the gate:** WO-1 first (RAM — a hard prerequisite for
WO-10's 4×4 numpad, which peaks at ~32 LVGL objects vs today's 24), then
WO-9 → WO-10 → WO-11 (user features, smallest to largest), then WO-2/3/8.
Re-`Read` `status_screen.c` before EVERY work order — each prior WO changed it, and
stale exact-match edits will fail or, worse, land in the wrong context.

---

## Current-state snapshot (verified 2026-07-03)

- **Branches:** `main` (stable; 1 ahead / 32 behind `dev/touch-screen` — the 1 is the
  theme-strip commit `b73570e`). `dev/touch-screen` = this whole arc, unmerged,
  gated on hardware (§9.6). `dev/periph-theme` = intentional backup of the full
  peripheral theme system (KEEP). `dev/fix-theme` = merged historical branch.
  `dev/touch-easy`, `dev/touch-testing`, `zmk-0-3` = stale experiments (WO-5).
- **Forks:** `H0N3YC4T/prospector-zmk-module` @ `feat/new-status-screens` (13 commits
  of UI); `H0N3YC4T/zmk` @ `fix/3156-deferred-subscribe` (2 commits on base
  `64daf698`). Both pinned in `config/west.yml` (rules §9.1-1..3).
- **Build matrix (build.yaml):** reset, reset-waveshare, dongle (nano), dongle-waveshare
  (XIAO+prospector), central-left, left, right. All green at HEAD.
- **Dongle RAM picture (the binding constraint):** at LVGL pool 25000, free RAM ≈
  2.7 KB (32768 overflowed by ~5 KB, empirical). Known consumers that are OURS to
  tune: LVGL pool (25 KB), the behavior queue (≈12.3 KB, see WO-1), USB logging
  (temporary, removed in §9.5 after reconnect verifies).

---

## WO-1 (HIGH) — Dongle RAM reclaim: behavior-queue right-sizing + LVGL headroom

**Issue:** `config/prototype_mk1.conf:35` sets `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=512`
globally ("MACRO MEMES" — sized for the halves' long macros). The queue is a static
`K_MSGQ_DEFINE` of `q_item` (≈24 B: u32 position + u8 source + 12 B binding + u32
bitfield → `zmk/app/src/behavior_queue.c:16-26`), so the **dongle** statically
spends 512×24 ≈ **12.3 KB of RAM** on a queue whose dongle-side load is 2 entries
per touch tap. This is why the LVGL pool hit a wall at ~25 KB.

**Decision:** override to 64 on the dongle only (halves keep 512 — their macros need
it), and spend part of the reclaimed RAM raising the LVGL pool 25000 → 28672 so the
numpad-blank-button contingency (§9.3) likely never triggers.

**Implementation (main repo only — NO fork changes):**
1. `build.yaml`, dongle-waveshare entry: the global conf merges LAST, so a shield
   .conf cannot override it; the established mechanism is cmake-args (precedent:
   `-DCONFIG_ZMK_EXT_POWER=n` on the same line). Change:
   ```yaml
   cmake-args: -DCONFIG_ZMK_EXT_POWER=n -DCONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=64
   ```
2. `boards/shields/prototype_mk1/prototype_mk1_waveshare.conf`: change
   `CONFIG_LV_Z_MEM_POOL_SIZE=25000` → `28672` and update its comment block: the
   32768-overflow history stays, add "queue right-sizing (build.yaml cmake-args)
   freed ~10.7 KB, part reinvested here".
3. Do NOT touch the nano-dongle or central-left entries (different RAM budgets,
   not at a wall, not worth the churn).
4. One commit, normal CI (no `[skip ci]`), message:
   `dongle: right-size behavior queue (512->64, ~10.7KB RAM) + LVGL pool to 28K`.

**Verify:** CI green is the whole test (the 32768 failure proved the linker enforces
this). Expected free RAM after: ≈ 2.7 + 10.7 − 3.7 ≈ **9.7 KB** — report the number
from the build log's memory region table if printed.

**Pitfalls:** don't move the 512 out of the global conf (halves rely on it; touching
their macro capacity risks breaking "MACRO MEMES" behavior). Don't set the queue
size in `prototype_mk1_waveshare.conf` — the global conf merges last and would
silently win; it MUST be cmake-args. Touch-UI depth check already done: `queue_key`
enqueues 2 items/tap, drained same-tick; 64 is 32 taps of headroom.

## WO-2 (MED) — Brightness readout on the Settings screen

**Issue:** brightness ∓ works blind — no on-screen level indication; the only
feedback is a serial log line.

**Decision:** the Settings screen's Back cell (row 0, col 1) shows the current
percentage under the arrow glyph. No new cells, no layout change.

**Implementation:**
1. Prospector fork, `src/brightness.c`: add an accessor next to the stepper:
   ```c
   uint8_t prospector_brightness_get(void) { return touch_brightness; }
   ```
2. Fork, `src/layouts/operator/status_screen.c`:
   - declare `extern uint8_t prospector_brightness_get(void);` next to the existing
     `prospector_brightness_step` extern;
   - `draw_cell()` already takes a text string; in `build_view()`'s `VIEW_SETTINGS`
     case, replace the Back cell call with a formatted two-line label:
     ```c
     char bl_lbl[16];
     snprintf(bl_lbl, sizeof(bl_lbl), LV_SYMBOL_LEFT "\n%d%%", prospector_brightness_get());
     draw_cell(0, 1, 1, bl_lbl, COLOR_BACK);
     ```
     (`lv_label_set_text` copies the string — a stack buffer is safe. `%` renders in
     montserrat_20; `\n` is supported by lv_label.)
   - in `handle_tap()`'s `VIEW_SETTINGS` cases 0 and 2, after calling
     `prospector_brightness_step(...)`, add `build_view(VIEW_SETTINGS);` so the
     readout refreshes (same pattern the Modifiers screen already uses).
3. Commit fork → push `fork feat/new-status-screens` → pin bump (§9.1 rules 1-3) → CI.

**Verify:** CI green; hardware: Settings shows `← / NN%`, updates on each tap,
clamps at 5 and 100.

**Pitfalls:** don't make the label a static/global updated in place — rebuilding the
view via `build_view` is the established idiom and avoids dangling `lv_obj` pointers
after `lv_obj_clean`. Keep the buffer ≥ 8 bytes (`←\n100%` + NUL).

## WO-3 (MED) — Armed-modifier indicator outside the Modifiers screen

**Issue:** a one-shot mod armed on the Modifiers screen is invisible once you
navigate to F-keys/Numpad/Symbols — you can't tell the next key will carry Ctrl.

**Decision:** while `pending_mods != 0`, the full-screen overlay gets a 3 px
battery-blue border (the "armed" color already used for active mods); border width 0
otherwise. Global, unmissable, zero layout impact.

**Implementation (fork, `status_screen.c`):** at the END of `build_view()` (after
the switch, so it applies to every screen including hub pages):
```c
lv_obj_set_style_border_color(overlay, lv_color_hex(COLOR_PAGE), LV_PART_MAIN);
lv_obj_set_style_border_width(overlay, pending_mods ? 3 : 0, LV_PART_MAIN);
```
Note `zmk_display_status_screen()` currently sets the overlay border width to 0 at
creation — leave that; build_view now owns it. `send_key()` clears `pending_mods`
but does NOT rebuild the view (the user is mid-typing; the border clears on the next
navigation) — accept that one-screen lag, do not add a rebuild to send_key (it would
redraw under the user's finger mid-tap-burst).
Commit → pin bump → CI, same cycle as always.

**Verify:** CI green; hardware: arm CTRL → blue frame appears and follows you into
F-keys; send a key → frame clears on next screen change; leave hub → `show_view`
clears `pending_mods` (existing behavior) and the frame is gone.

## WO-4 (MED) — Fork maintenance: rebase procedure + optional upstream PR

**Issue:** two personal forks are now load-bearing. Future zmk/prospector bumps need
a documented procedure, and the `central.c` fix addresses an OPEN upstream bug
(zmk #3156) — upstreaming it removes our long-term maintenance burden.

**Decision:** (a) document the rebase procedure in `MIGRATION_NOTES.md`; (b) prepare
— but do NOT open without explicit user consent (outward-facing) — an upstream PR.

**Implementation:**
1. Append to `MIGRATION_NOTES.md`, section "Fork maintenance":
   - zmk: `cd _touchref/zmk && git fetch origin && git rebase origin/main
     fix/3156-deferred-subscribe` → resolve (the patch touches only
     `app/src/split/bluetooth/central.c`; if upstream refactored it, port the four
     elements: deferred flush, per-subscription disc_params, release-time
     value+ccc clearing, position self-heal) → `git push -f fork
     fix/3156-deferred-subscribe` → pin bump → full CI → hardware re-test of
     reconnect before merging the bump.
   - prospector: same shape, branch `feat/new-status-screens`, conflicts likely only
     in `status_screen.c`/`brightness.c` (wholly ours — take ours).
2. PR prep (docs only until consent): a clean branch off `zmkfirmware/main` with two
   commits (deferred-subscribe+disc-params separation; self-heal), PR body from
   `ZMK-3156-DEEP-DIVE.md` §3.4-3.5 + REVIEW-BRIEF §8a (the shared-struct clobber
   analysis). **Ask the user before pushing anything to a PR.**

## WO-5 (LOW) — Branch hygiene

**Issue:** `dev/touch-easy`, `dev/touch-testing`, `zmk-0-3` are stale experiments;
`dev/fix-theme` is merged history.
**Decision:** propose deletion to the user with a one-line description of each
(check `git log -3 --oneline <branch>` first); delete only what they approve.
`dev/periph-theme` is a deliberate backup — NEVER propose deleting it.

## WO-6 (LOW, post-merge only) — Docs consolidation

**Issue:** six overlapping docs, some sections superseded (CODE-REVIEW §8 → brief
§0; deep-dive §3.9's "=20 should suffice" → disproven).
**Decision (dispositions):** after `dev/touch-screen` merges to main —
- KEEP live: `REVIEW-BRIEF.md` (rename `DONGLE-NOTES.md`; it is the operational
  manual: §9 rules + contingencies), `MIGRATION_NOTES.md`.
- ARCHIVE (move to `docs/history/`, add a one-line "superseded by" header):
  `CODE-REVIEW.md`, `ZMK-3156-DEEP-DIVE.md`, `TOUCH-SCREEN-NOTES.md`,
  `FIX-THEME-NOTES.md`, `IMPROVEMENT-PLAN.md` (this file, once WOs are done).
- Do not rewrite archived content; headers only. `[skip ci]`.

## WO-7 (LOW) — Conf comment parity (comment-only, zero functional change)

**Issue:** two confs carry pre-fix-era settings that could mislead a future reader:
`prototype_mk1_dongle.conf:13` `CONFIG_BT_MAX_CONN=7` (nano dongle; predates the
"BT_MAX_CONN is not the fix" finding) and `prototype_mk1_central_left.conf` (relies
on zmk-default `BT_ATT_TX_COUNT=10`, no note that the fork's central.c fix is what
actually protects it).
**Decision:** comments only. Do NOT change either value (the nano dongle is a
working fallback device; churn = risk, benefit = none). Add one comment to each
pointing at `REVIEW-BRIEF.md` §8a for the reconnect story. `[skip ci]` not allowed
(conf files are build inputs — run normal CI even though output should be identical).

## WO-8 (LOW) — Hub label polish

**Issue:** hub cells read `F` / `123` / `#` / `MOD` — terse placeholders (plus `PAD`
once WO-11 lands).
**Decision:** `F1-12`, `789`, `#$%`, `PAD`, `MOD` (all pure ASCII — montserrat_20-safe;
do NOT try lowercase or exotic glyphs, and do not switch to LV_SYMBOL icons — none
of the available symbols reads unambiguously for these). Run AFTER WO-11 so the PAD
cell exists.
**Implementation:** string literals in `build_view()`'s `VIEW_HUB` case; fork
commit → pin bump → CI.

---

# User-requested feature work orders (2026-07-03)

All in the prospector fork's `status_screen.c` unless stated; WO-11 also touches the
main repo's `touch_input.c`. Every fork commit follows §9.1 rules 1-3 (push to
`fork feat/new-status-screens`, exact-SHA pin bump, CI green before the next WO).
Two places below deviate from the user's literal wording because the literal version
is geometrically or gesturally impossible — both are marked **DESIGN DECISION** and
should be confirmed with the user if they review before execution; otherwise
implement as specified here.

## WO-9 (USER) — Home screen: "MACRO" → "KEYS" + wider bottom-row buttons

**Issue:** (1) the home cell 5 label should read `KEYS`; (2) the three bottom
buttons (`MEDIA` / `SET` / `KEYS`) are 80%-of-cell wide (≈74 px) and 5-glyph labels
sit tight.

**Implementation:**
1. Generalize width: rename the body of `draw_cell()` into
   `draw_cell_pct(int row, int col, int w_cells, const char *text, uint32_t accent, int pct)`
   with `bw = cw * pct / 100` (height stays `ch * 4 / 5`), and re-create
   `draw_cell(...)` as a one-line wrapper calling `draw_cell_pct(..., 80)`. No other
   call sites change.
2. `VIEW_HOME` case: label `"MACRO"` → `"KEYS"`, and the three bottom cells use
   `draw_cell_pct(1, col, 1, <label>, COLOR_ACCENT, 92)` (92% ≈ 85 px; adjacent
   buttons keep ≈7 px visual separation).
3. Update the nav-map comment block at the top of the file and the nav maps in
   `REVIEW-BRIEF.md` §"What was built" / `TOUCH-SCREEN-NOTES.md` in the same commit.

**Pitfalls:** touch cells are FIXED thirds — visual width has zero effect on
`cell_from_coords()`; do not touch the mapper. Do not exceed ~94% width (borders
visually merge). If `MEDIA` still clips at 92%, the fix is a shorter label (`MUSIC`?
ask user) — NOT a new font (extra montserrat sizes cost flash and a Kconfig; banned
here).

## WO-10 (USER) — Grid generalization: 3×3 F-keys/Symbols + 4×4 numpad with operators

**Issue:** (a) F-keys and Symbols should show more keys per page (3 rows × 3 cols,
was 2×3); (b) the numpad needs `+ - * /`.

**DESIGN DECISION (operators):** the user asked for "a 4th row with plus, minus,
multiply and divide" — four keys cannot fit a row of a 3-column grid, and a mixed
3/4-column screen breaks the uniform cell mapper. Implemented instead as a **4×4
numpad** with the operators as the right-hand column, top→bottom in the user's
listed order (`+ - * /`). Final layout (cell indices for a 4-column grid,
`cell = row*4 + col`):
```
 7  8  9  +        cells  0  1  2  3
 4  5  6  -        cells  4  5  6  7
 1  2  3  *        cells  8  9 10 11
 ←  0  ⏎  /        cells 12 13 14 15
```

**Implementation:**
1. Add `static int grid_cols = 3;` beside `grid_rows`. In `build_view()`, set both
   per view: NUMPAD → 4×4; FKEYS/SYMBOLS → 3 rows × 3 cols; everything else 2×3.
2. Replace every `CELL_W` use with a computed `SCR_W / grid_cols` (in
   `draw_cell_pct` and `cell_from_coords`); clamp `col` to `grid_cols - 1`; return
   `row * grid_cols + col`. Delete the now-unused `CELL_W`/`CELL_H` macros.
3. Paginated pages: `KEYS_PER_PAGE` 4 → **7**; `key_cells` → `{0, 2, 3, 4, 5, 6, 8}`
   (all cells except 1 and 7). Nav keeps its vertical symmetry: cell 1 (top-middle)
   = Back on page 0 / `LV_SYMBOL_UP` prev-page on page ≥1; **cell 7 (bottom-middle)**
   = `LV_SYMBOL_DOWN` next-page — update `draw_key_page`'s next-page call from
   `draw_cell(1, 1, ...)` to `draw_cell(2, 1, ...)` and `handle_key_page`'s
   `if (cell == 4)` to `if (cell == 7)`. Resulting pages: F-keys 2 (7+5), Symbols 5
   (7×4+4). `draw_key_page`/`handle_key_page` already guard `idx < n`, so
   partially-filled last pages need no new code.
4. Numpad: rewrite the `VIEW_NUMPAD` draw block per the layout above (labels
   `"+" "-" "*" "/"` — pure ASCII; `÷`/`×` are NOT in montserrat_20's default glyph
   range and render tofu). Handler table becomes:
   ```c
   static const uint32_t np[16] = {N7, N8, N9, PLUS,  N4, N5, N6, MINUS,
                                   N1, N2, N3, STAR,  0,  N0, RET, FSLH};
   if (cell == 12) { show_view(VIEW_HUB); }
   else if (cell >= 0 && cell <= 15 && np[cell]) { send_key(np[cell]); }
   ```
   `PLUS`/`MINUS`/`STAR`/`FSLH` come from the already-included `keys.h` (verified
   present: keys.h lines 233/222/176/321). They are the shifted main-row codes —
   deliberately NOT the `KP_*` keypad codes, whose digit siblings are
   NumLock-dependent; output is identical characters on every host.
5. Update nav maps in the file-top comment + docs, same commit.

**Pitfalls:** this WO **requires WO-1 first** — the 4×4 numpad draws ~32 LVGL
objects (was 24, the level that crashed at a 20 KB pool); WO-1's 28672 pool plus the
existing NULL-guards cover it (~+1.3 KB over today's peak; guards degrade a
shortfall to a missing button, never a crash). Cell indices are now
`row*grid_cols+col` — every hardcoded cell number in `handle_tap` for NUMPAD changed
(back was 9, is now 12); do not touch the 2×3 screens' handlers (their indices are
unchanged because their grid stays 3-wide). `grid_cols`/`grid_rows` are read and
written on the display thread only — keep it that way.

## WO-11 (USER) — Trackpad: hub button + full-screen pointer page

**Issue:** hub cell 4 (currently empty) opens a trackpad page: the whole screen
moves the host pointer; **double-tap = left click; tap-and-hold = right click**.

**DESIGN DECISION (exit):** with the whole surface consumed by pointer gestures,
every exit gesture conflicts with the two click gestures (single-tap must stay a
no-op — it is the first half of a double-tap). Exit = **a tap that starts inside the
top-left 40×40 px corner**, marked with a small red `LV_SYMBOL_LEFT` glyph. Motion
that merely passes through the corner still moves the pointer; only a discrete
corner tap exits. No idle timeout (hub convention: the user parks on these pages).

**Architecture (decided — do not restructure):** all gesture/pointer logic lives in
the main repo's `app/src/touch/touch_input.c` (it owns the raw event stream); the
fork only renders the page, reports "trackpad mode" via a weak hook, and handles the
exit tap. HID goes direct (not via behaviors) from a `k_work` on the system
workqueue — same thread rule as §9.1-6. Verified API (zmk pinned fork,
`include/zmk/hid.h:371-377`, `include/zmk/endpoints.h:106`, guarded by
`CONFIG_ZMK_POINTING` which is already `=y` globally):
`zmk_hid_mouse_movement_set(int16_t x, int16_t y)`,
`zmk_hid_mouse_buttons_press/release(zmk_mouse_button_flags_t)`,
`zmk_endpoint_send_mouse_report()`; button flags `MB1`/`MB2` from
`<dt-bindings/zmk/pointing.h>`.

**Implementation — fork (`status_screen.c`):**
1. Add `VIEW_TRACKPAD` to the enum (after `VIEW_MODIFIERS`; it is ≥ `VIEW_HUB` so
   the no-timeout rule already covers it).
2. Hub: `draw_cell(1, 1, 1, "PAD", COLOR_ACCENT);` and `case 4: show_view(VIEW_TRACKPAD); break;`
   in the `VIEW_HUB` handler.
3. `build_view` `VIEW_TRACKPAD`: black screen with (a) a `lv_label` `LV_SYMBOL_LEFT`
   in `COLOR_BACK` at position (8, 4) — the exit affordance; (b) a centered dim hint
   label `"PAD"` colored `lv_color_hex(0x303030)`.
4. `handle_tap` `VIEW_TRACKPAD`: **any** tap → `show_view(VIEW_HUB);` (touch_input
   only forwards corner taps in this mode — see below).
5. The mode hook, next to `prospector_touch_tap`:
   ```c
   bool prospector_touchpad_active(void) { return cur_view == VIEW_TRACKPAD; }
   ```
   (Read cross-thread from the input path; `cur_view` is a single aligned int —
   worst case one event routed under the stale mode for one 30 ms tick. Accepted;
   do not add locking.)

**Implementation — main repo (`touch_input.c`):**
1. Weak default beside the existing one:
   `__weak bool prospector_touchpad_active(void) { return false; }`
2. Includes (guard the whole trackpad block with
   `#if IS_ENABLED(CONFIG_ZMK_POINTING)`): `<zmk/hid.h>`, `<zmk/endpoints.h>`,
   `<dt-bindings/zmk/pointing.h>`.
3. State: `static atomic_t tp_dx, tp_dy;` accumulated deltas;
   `static atomic_t tp_click;` (0 none / MB1 / MB2); previous-point + tap-time
   statics (input-callback context only, no atomics needed for those).
4. In `touch_cb`, when `prospector_touchpad_active()`:
   - Use `evt->sync` (final event of each report) as the sample point — compute the
     screen-coords point via the existing `panel_to_screen_x/y`, delta against the
     previous sample, `atomic_add(&tp_dx/dy, delta)`, submit `tp_work`. **On
     BTN_TOUCH press, seed prev = current with NO delta** (else the pointer jumps).
   - Tap-and-hold: `k_work_schedule(&tp_hold_work, K_MSEC(400))` on touch-down;
     cancel (`k_work_cancel_delayable`) on release OR when total travel exceeds
     `TOUCH_TAP_MAX_TRAVEL`. If it fires (still touching, travel small):
     `atomic_set(&tp_click, MB2); k_work_submit(&tp_work);` and set a
     `hold_fired` flag suppressing tap classification for this contact.
   - Double-tap: on a release that classifies as a tap (existing travel test,
     duration < 250 ms, `!hold_fired`): if it **started in the corner**
     (`start sx < 40 && start sy < 40`, screen coords) → forward via the existing
     `prospector_touch_tap(sx, sy)` path (= exit). Else if the previous qualifying
     tap's release was < 350 ms ago → `atomic_set(&tp_click, MB1);
     k_work_submit(&tp_work);` and clear the stored time; else store this release
     time and do nothing (single taps are silent).
   - Skip the normal tap-dispatch path entirely while in trackpad mode (except the
     corner forward above).
5. `tp_work` handler (system workqueue — the only place HID is touched):
   ```c
   int dx = atomic_set(&tp_dx, 0), dy = atomic_set(&tp_dy, 0);
   int click = atomic_set(&tp_click, 0);
   if (dx || dy) {
       zmk_hid_mouse_movement_set((int16_t)dx, (int16_t)dy);
       zmk_endpoint_send_mouse_report();
       zmk_hid_mouse_movement_set(0, 0);
   }
   if (click) {
       zmk_hid_mouse_buttons_press(click);
       zmk_endpoint_send_mouse_report();
       zmk_hid_mouse_buttons_release(click);
       zmk_endpoint_send_mouse_report();
   }
   ```
6. Sensitivity knobs at the top: `#define TP_SENS_NUM 1`, `#define TP_SENS_DEN 1`
   applied as `delta * TP_SENS_NUM / TP_SENS_DEN` — first hardware pass tunes these.

**Verification:** CI green (both repos' changes land in ONE coordinated push: fork
commit + pin bump + touch_input.c in the same main-repo commit — the weak hooks keep
either side buildable alone, but test them together). Hardware: pointer tracks a
finger; direction errors mean the delta needs the same axis treatment the tap
transform got (expect ONE calibration iteration, like the original rotation fix —
the transform reuse should make it right first try, but verify before tuning
sensitivity). Double-tap clicks; ~½-second hold right-clicks; corner tap returns to
the hub; Media/Settings/keys pages behave exactly as before when NOT in trackpad
mode.

**Pitfalls:**
- **Never call any `zmk_hid_*` or `zmk_endpoint_*` from `touch_cb`** — it runs in
  the input driver's context. Everything HID goes through `tp_work` (§9.1-6 class
  of bug; it produced the boot-variant key corruption).
- `evt->sync` gating matters: computing deltas per `INPUT_ABS_X`/`_Y` event
  double-counts each report (X and Y arrive as separate events).
- `k_work_cancel_delayable` on release is mandatory — a hold-work firing after
  release right-clicks on a completed tap.
- The function is `zmk_endpoint_send_mouse_report()` — singular "endpoint", no "s".
- Do NOT route clicks through `zmk_behavior_queue_add`/`mouse_key_press` — the
  direct-HID path above is fewer moving parts and already on the right thread.
- Do NOT try pinch/scroll/drag or two-finger anything — this CST816S driver
  reports a single touch point. Out of scope: drag (double-tap-hold), scroll.
- The corner-exit tap reuses `prospector_touch_tap`, so it inherits the existing
  atomic single-slot hand-off — fine (exit is a single discrete action).

---

## Explicitly NOT recommended (do not do these)

- **No `status_screen.c` modularization/refactor.** It's a 470-line file that is
  fully understood and hardware-unverified; splitting it now trades known-good
  structure for regression risk with zero user-visible benefit.
- **No further LVGL pool increases beyond WO-1's 28672** without a new RAM audit —
  and no pool increase INSTEAD of WO-1 (the queue is the actual waste).
- **No nano-dongle or central-left functional changes** (working fallbacks).
- **No re-visiting** BT_MAX_CONN / BT_ATT_TX_COUNT>20 / L2CAP buffer knobs (§9.1-10).
- **No new build.yaml targets** and no removal of the nano-dongle target (it is the
  user's fallback receiver).
- **No upstream PR, branch deletion, or merge to main without explicit user
  consent** — each is outward-facing or destructive.
