# Project review & forward work orders

Planned by Fable 5 (2026-07-03) for execution by Opus 4.8. Every design decision is
already made here — the executor implements, verifies, and documents; it does not
re-litigate choices or invent alternatives. If an instruction conflicts with
reality (file moved, SHA stale, CI red), STOP and report rather than improvise.

**Binding rules:** `REVIEW-BRIEF.md` §9.1 (operational rules — all eleven apply to
every work order). One work order per commit cycle (fork commit → pin bump → CI
green → docs), never batched. Any CI red: fix or revert before starting the next WO.

**Sequencing gate:** WO-1/2/3/8 change the dongle build and MUST wait until the
user's hardware round (§9.2 of REVIEW-BRIEF.md) passes on the current state
(`dev/touch-screen` @ `a5f038b`, zmk fork `c2ca24f`, prospector fork `62ba9e5`).
Layering new changes before that verification would muddy the test. WO-4/5/6/7 are
docs/hygiene and may run anytime.

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

**Issue:** hub cells read `F` / `123` / `#` / `MOD` — terse placeholders.
**Decision:** `F1-12`, `789`, `#$%`, `MOD` (all pure ASCII — montserrat_20-safe; do
NOT try lowercase or exotic glyphs, and do not switch to LV_SYMBOL icons — none of
the available symbols reads unambiguously for these four).
**Implementation:** four string literals in `build_view()`'s `VIEW_HUB` case; fork
commit → pin bump → CI.

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
