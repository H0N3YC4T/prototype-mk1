# Review brief — dongle touch UI + recent bug fixes (technical, for handoff)

**Audience note:** this handoff includes the full chat transcript as context, so the
reviewer has already seen every `Read`/`Edit`/build-log tool call that produced this
state — including two abandoned intermediate versions of the key-send code. **This
document is the authority on final state.** Where the transcript and this doc
disagree (they shouldn't, but chronology is easy to misread), trust this doc and the
line numbers/SHAs cited here, which were captured by re-reading the files fresh
immediately before writing this brief.

Branch: **`dev/touch-screen`**, HEAD `3a07ed9`. Unmerged to `main`. **All CI green,
including §0's correction — confirmed green (run completed successfully) before this
brief was finalized, not left as a promise.** `config/west.yml` line 32 pins the
prospector fork to `62ba9e5c94efea412eb2776cc889b0bafe2e6c5f`, which is the SHA that
built; treat that file as ground truth over any SHA quoted elsewhere.

---

## §0. Late correction made during this same session — READ THIS FIRST

While writing this brief I re-derived the number-key-corruption fix (issue #4 below)
from source and found **the fix committed earlier in this session was itself broken**
— a regression introduced when simplifying it. This is now corrected. Full mechanism:

**The bug in the "fix":** `status_screen.c`'s `queue_key()` originally (transcript
commit `8ad5960`) called `zmk_behavior_queue_add(&ev, b, true/false, 0)` directly from
`ui_timer_cb()` — i.e. from the **LVGL display thread** — believing this routed the
invoke onto ZMK's own thread context (mirroring how ZMK macros use it). It does not,
in the common case. `zmk_behavior_queue_add()` in `zmk/app/src/behavior_queue.c`:
```c
int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                           const struct zmk_behavior_binding binding, bool press, uint32_t wait) {
    struct q_item item = { ... };
    const int ret = k_msgq_put(&zmk_behavior_queue_msgq, &item, K_NO_WAIT);
    if (ret < 0) { return ret; }
    if (!k_work_delayable_is_pending(&queue_work)) {
        behavior_queue_process_next(&queue_work.work);   // <-- PLAIN FUNCTION CALL
    }
    return 0;
}
```
`behavior_queue_process_next()` only gets deferred to the workqueue thread via
`k_work_schedule()` when a previously-processed item had `wait > 0`. Our calls always
pass `wait=0` for both press and release, so `k_work_schedule` never fires, the
delayable is essentially never "pending", and the `if (!k_work_delayable_is_pending)`
branch is taken essentially every time — meaning `behavior_queue_process_next()` (and
thus `zmk_behavior_invoke_binding()`, which writes the shared HID report) executes
**synchronously, inline, on whatever thread called `zmk_behavior_queue_add`** — the
LVGL display thread, unchanged from the original bug. The msgq copy is real but
irrelevant to thread-safety; this "fix" only added indirection, not a thread hop.

**Corrected fix (current `status_screen.c`, fork commit `62ba9e5`):** restores an
explicit `k_work_submit()` to the system workqueue — which Zephyr guarantees always
runs the handler on the workqueue's own thread, never inline in the caller — and
calls `zmk_behavior_queue_add()` **from inside that handler**, where it's now safe
(and gets the bonus of serialising against any in-flight delayed macro, since both
run on the same single-threaded system workqueue). Full corrected code:
```c
struct pending_key { const char *dev; uint32_t param1; };
#define KEY_RING_SZ 8
static struct pending_key key_ring[KEY_RING_SZ];
static atomic_t key_ring_head = ATOMIC_INIT(0);
static atomic_t key_ring_tail = ATOMIC_INIT(0);

static void key_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    while (atomic_get(&key_ring_tail) != atomic_get(&key_ring_head)) {
        struct pending_key pk = key_ring[atomic_get(&key_ring_tail) % KEY_RING_SZ];
        struct zmk_behavior_binding b = {.behavior_dev = pk.dev, .param1 = pk.param1};
        struct zmk_behavior_binding_event ev = {.layer = 0, .position = 0x7000,
                                                .timestamp = k_uptime_get()};
        zmk_behavior_queue_add(&ev, b, true, 0);   // safe here: system workqueue thread
        zmk_behavior_queue_add(&ev, b, false, 0);
        atomic_inc(&key_ring_tail);
    }
}
static K_WORK_DEFINE(key_work, key_work_handler);

static void queue_key(const char *dev, uint32_t param1) {
    int h = atomic_get(&key_ring_head);
    key_ring[h % KEY_RING_SZ] = (struct pending_key){.dev = dev, .param1 = param1};
    atomic_set(&key_ring_head, h + 1);
    k_work_submit(&key_work);   // real, unconditional thread hop
}
```
`send_key()`/`fire_macro()` are unchanged, both still funnel through `queue_key()`.

**Swept for recurrence:** grepped every changed file in both forks for
`zmk_behavior_invoke_binding|zmk_behavior_queue_add`. Only two call sites exist
outside this one: `app/src/touch/touch_input.c:117-118` (`touch_fire()`, invoked via
its own `k_work_submit(&touch_work)` from `touch_cb()` — already correct, unaffected,
predates this whole feature) and the corrected site above. No other instance of the
anti-pattern found.

**What to re-verify:** (a) that `k_work_submit()` on a `K_WORK_DEFINE` (non-delayable)
item is unconditionally thread-hopping and cannot short-circuit like the delayable
variant did — this is the load-bearing assumption of the whole fix; (b) that calling
`zmk_behavior_queue_add()` from within a `k_work` handler that is itself running *on*
the system workqueue doesn't deadlock or reenter oddly (it shouldn't — `queue_add`'s
own fast path is just a function call, not a recursive submit, and our handler isn't
holding any lock `queue_add` needs) — but this is exactly the kind of assumption a
second reviewer should independently confirm against Zephyr's actual `k_work.c`.

CI status for this fix: fork `62ba9e5c94efea412eb2776cc889b0bafe2e6c5f`, pinned in
`config/west.yml` line 32, `dev/touch-screen` commit `3a07ed9` — **build confirmed
green** (GitHub Actions run completed with conclusion `success`) before this brief
was finalized. Compile-correctness is confirmed; logical correctness (the actual
subject of §7 item 1) is not, and is exactly what needs a second opinion.

---

## §1. System context

- **Hardware:** Seeed XIAO nRF52840 + Waveshare 1.69" ST7789 color LCD (280×240
  landscape render, `mipi-dbi-spi` over `&spi3`) + CST816S capacitive touch
  (I²C `&i2c1`, address `0x15`, IRQ→D0, RST→D1, SDA→D4, SCL→D5) = the "dongle"
  (BLE split central). Two nice!nano v2 halves are peripherals; talks to host over
  BLE with 3 profiles (`BT_MAX_PAIRED=5`, `ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=2`,
  5-2=3 profiles). A second, dongleless boot variant (`prototype_mk1_central_left`)
  also exists and shares the reconnect-fix pin but not the touch UI.
- **Firmware:** ZMK **v0.3.0**, Zephyr **v4.1.0+zmk-fixes** (`zmk/app/west.yml`).
  RAM headroom is tight: bumping the dongle's LVGL pool from 20 KB to 32 KB
  overflowed the RAM region by ~5 KB (linker error, confirmed by CI), so effective
  free RAM above the 20 KB baseline is only ~7.7 KB. This constrains any future
  memory-hungry change on this target.
- **Why two forks exist:** (1) `H0N3YC4T/prospector-zmk-module` — carries the OPERATOR
  theme customizations (WPM/battery colors) that predate this session, now also
  carries the entire touch UI + the brightness fix. (2) `H0N3YC4T/zmk` — new this
  session, exists solely to carry the `central.c` reconnect fix (§ Issue 1); ZMK core
  has never been forked before in this project.

## §2. Pins (`config/west.yml`) — verify against the live file, not this table

| project | remote | revision (as of writing) | purpose |
|---|---|---|---|
| `zmk` | `honeycat` (fork of zmkfirmware) | `c027b34f0a2bb1a30e31df1ba34653fe7063128f` | branch `fix/3156-deferred-subscribe`; base = `zmkfirmware @ 64daf698e073e37b6748ac54f4eb48d8666af0b9` |
| `prospector-zmk-module` | `honeycat` | `62ba9e5c94efea412eb2776cc889b0bafe2e6c5f` | branch `feat/new-status-screens`; see §0 for the just-landed correction |
| `zmk-dongle-display` | `englmaxi` | `2bb333f87136d33e94a49d86236ed9ec254a8060` | untouched this session |

**Revert levers**, in case a bisect is needed:
- Reconnect fix only: point `zmk`'s `revision:` back to `zmkfirmware @ 64daf698e073e37b6748ac54f4eb48d8666af0b9` (its unforked, unpatched base) and remove `remote: honeycat` → `remote: zmkfirmware`.
- Touch UI / brightness: the fork's commit history (`feat/new-status-screens`) is
  linear and each commit built green individually — bisectable commit-by-commit if
  needed; see `git log` on that branch or the commit list in §5.

## §3. Files changed, with exact scope

| file | repo | what changed |
|---|---|---|
| `boards/shields/prospector_adapter/src/layouts/operator/status_screen.c` | prospector fork | Entire touch UI (~420 lines): navigation state machine, all 8 views, key-send plumbing (§0). |
| `boards/shields/prospector_adapter/src/brightness.c` | prospector fork | `pwm_leds_dev` binding changed from `DEVICE_DT_GET_ONE(pwm_leds)` to `DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)))` (line 16); added `LOG_INF` in `prospector_brightness_step()` (line 28). Rest of file (ambient-light-sensor branch, `#else` fixed-brightness `SYS_INIT`) untouched. |
| `boards/shields/prospector_adapter/boards/xiao_ble_zmk.overlay` | prospector fork | **Unchanged this session** — included here because it defines `disp_bl` (line 4: `disp_bl: pwm_led_1 { pwms = <&pwm1 0 ...>; }`), the node the brightness fix targets, and the `apds9960@39` node on the shared `&i2c1` bus that co-exists with our `cst816s@15`. |
| `app/src/split/bluetooth/central.c` | **zmk fork** (new) | `split_central_chrc_discovery_func()` and helpers — see §0-adjacent Issue 1 for the diff. First-ever fork of ZMK core in this project. |
| `app/src/touch/touch_input.c` | main repo | `prospector_touch_tap()` signature changed from `(int cell)` to `(int sx, int sy)` (raw coords, to support the numpad's 4×3 grid); `touch_fire()` now calls the new signature and falls back to its own `touch_cell()` 2×3 mapper only if the fork returns `false`. |
| `boards/shields/prototype_mk1/prototype_mk1_waveshare.overlay` | main repo | Added `touch_macro_3/4/5` (play-pause/prev/next) alongside the pre-existing `touch_macro_0/1/2` (vol down/mute/vol up). Keyboard-backlight relay placeholder (`&pwm0`, P0.08) and `cst816s@15` node predate this session's fixes but are load-bearing context for Issue 2. |
| `boards/shields/prototype_mk1/prototype_mk1_waveshare.conf` | main repo | Added `CONFIG_LV_Z_MEM_POOL_SIZE=25000` (Issue 5). `CONFIG_BT_ATT_TX_COUNT=20` predates this session (was the original, insufficient reconnect mitigation — see Issue 1). `CONFIG_ZMK_USB_LOGGING=y` is deliberately still on for reconnect-log capture — remove for daily firmware once Issue 1 is hardware-confirmed. |

## §4. Issues found, root cause, fix — full technical detail

### Issue 1 — Split reconnect fails silently after sleep (ZMK #3156)
**Symptom:** a half re-establishes its BLE link after deep-sleep/wake but never
resumes sending keys; only a **dongle** power-cycle (not a half reset) clears it.

**Root cause chain** (fully sourced in `ZMK-3156-DEEP-DIVE.md`, condensed here):
1. `central.c`'s characteristic-discovery walk (`split_central_chrc_discovery_func`,
   type `BT_GATT_DISCOVER_CHARACTERISTIC`) calls `split_central_subscribe()` **inline**
   the moment it matches position-state, sensor, or battery characteristics —
   *while the walk itself is still iterating*.
2. `bt_gatt_subscribe()` auto-discovers the CCC descriptor if unknown
   (`BT_GATT_AUTO_DISCOVER_CCC`), i.e. it starts a **second, nested** `bt_gatt_discover`
   call using a `disc_params` struct. Before this fix, position-state and battery
   **shared one struct** (`slot->sub_discover_params`), so two nested discoveries could
   stomp each other.
3. Under load (2 peripherals reconnecting concurrently + battery-fetch + active host
   link), the nested discovery can fail with `-ENOMEM` (ATT buffer exhaustion).
   Zephyr's `gatt_discover_next()` reports *any* discovery termination — success or
   `-ENOMEM` — identically, by calling the callback with `attr == NULL`. ZMK's
   `if (!attr) { LOG_DBG("Discover complete"); return BT_GATT_ITER_STOP; }` cannot
   distinguish the two. So a starved nested discovery is silently treated as a clean
   walk completion, **before position-state was ever matched/subscribed**.
4. `CONFIG_BT_ATT_TX_COUNT=20` (already in `prototype_mk1_waveshare.conf`, confirmed
   applied — not shadowed by `config/prototype_mk1.conf`) widens the buffer pool but
   does not remove the **concurrency** (two discoveries in flight at once); the user
   confirmed on hardware that the failure recurred at `=20` — buffer size was never
   the true binding constraint.

**Fix** (`central.c`, fork `c027b34`, +46/-6 net across the file):
- Struct `peripheral_slot` gains `batt_lvl_sub_discover_params` (own CCC discover
  struct for battery; no longer shares `sub_discover_params` with position-state).
- New `static void split_central_flush_subscriptions(struct bt_conn *conn, struct peripheral_slot *slot)`
  — issues the position-state / sensor / battery `split_central_subscribe()` calls
  (each guarded on `value_handle` being non-zero) **and** the battery `bt_gatt_read()`,
  all in one place, all *after* discovery.
- The three inline `split_central_subscribe()` calls that used to fire the instant a
  characteristic matched (lines ~570, ~583, ~632 in the pre-fix file) are removed;
  the code now only *records* the handle into the subscribe-params struct.
- `flush_subscriptions()` is called from exactly two places, verified mutually
  exclusive per walk: (a) the `attr == NULL` "Discover complete" branch (natural end
  of walk — covers a peripheral missing an expected characteristic), and (b) the
  `subscribed` early-`BT_GATT_ITER_STOP` branch (all expected handles found before
  the walk would have ended naturally). Both call `peripheral_slot_for_conn(conn)`
  fresh and NULL-check it.
- `bt_gatt_subscribe()` returns `-EALREADY` on re-invocation with the same params, so
  `flush_subscriptions` is safe even if somehow called twice for one slot (it
  currently isn't, per the mutual-exclusivity above, but this makes it robust anyway).
- `ZMK_KEYMAP_HAS_SENSORS` and `CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS` paths
  preserved (sensor subscribe deferred the same way; HID-indicators path untouched,
  it was already handle-only, not an inline subscribe).
- **Deliberately NOT changed:** `CONFIG_ZMK_INPUT_SPLIT` path (line ~709, the input
  CCC-descriptor discovery's own `split_central_subscribe` call) — left inline. This
  build has `CONFIG_ZMK_INPUT_SPLIT` **off** (no peripheral pointing devices), so it's
  dead code for us, but flag this explicitly: **if input-split is ever turned on, it
  needs the identical deferral treatment or it reintroduces the same class of bug for
  that path.**

**Risk assessment:** highest-risk change in the entire set — it's core ZMK BLE
central code, touched for the first time in this fork's history. A regression would
manifest as *complete* peripheral input failure (not intermittent), which is easy to
detect and trivially revertable (§2 revert lever). **Zero hardware verification yet.**

### Issue 2 — Display brightness control does nothing
**Root cause:** `xiao_ble_zmk.overlay` (prospector fork, line 1-8) defines
`disp_bl: pwm_led_1` under a `pwmleds { compatible = "pwm-leds"; }` node on `&pwm1`.
`prototype_mk1_waveshare.overlay` (main repo, lines 40-52) *separately* defines
`backlight: pwmleds { compatible = "pwm-leds"; pwm_led_0 { ... }; }` on `&pwm0` — a
placeholder so `CONFIG_ZMK_BACKLIGHT`'s `zmk,backlight` chosen-node assert is
satisfied and the GLOBAL-locality `&bl` behavior can relay to the halves (P0.08 is an
otherwise-unused pad; this placeholder predates this session). **Two independent
`pwm-leds` compatible nodes exist in the final devicetree.** `brightness.c`'s original
`static const struct device *pwm_leds_dev = DEVICE_DT_GET_ONE(pwm_leds);` asserts
there is exactly one `okay` node of that compatible — with two present, which one it
resolves to is a devicetree/build-order matter, not something the C code controls,
and in practice it bound the **placeholder** (P0.08, physically unconnected) —
so every `led_set_brightness()` call, including the boot-time
`init_fixed_brightness()` `SYS_INIT` (line 170-176, unrelated to this session's UI
work), wrote to a dead pin. The display's actual backlight PWM (`&pwm1`, `P1.11`,
`nordic,invert` per `pinctrl` — line 34-36 of `xiao_ble_zmk.overlay`) was never
touched, so it sat at whatever level it powers up to.

**Fix:** `pwm_leds_dev` now resolved via
`DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)))` — walks up from the specific,
named `disp_bl` label to *its* parent `pwm-leds` device, which is unambiguous
regardless of how many other `pwm-leds` nodes exist elsewhere in the tree. `DISP_BL`
(the LED index within that device, via `DT_NODE_CHILD_IDX`) is unchanged. Added
`LOG_INF("display brightness -> %d%% (delta %d, rc %d)", ...)` so hardware testing
can confirm both that the call fires and its `led_set_brightness()` return code.

**Residual risk:** this assumes the panel's physical backlight is in fact wired to
`P1.11` per the DT — if the fix still shows no visible change but the log prints
`rc 0`, that would point to a hardware/pinout mismatch rather than a software bug
(disambiguation is the point of the added log line). Zero hardware verification yet.

### Issue 3 — One-shot modifier persists past intended scope
**Root cause:** `pending_mods` (armed via the Modifiers screen, cells 0/2/3/5 XOR one
of `MOD_LCTL/LSFT/LALT/LGUI`) is only cleared by `send_key()` after actually sending a
key (`status_screen.c` line ~120: `pending_mods = 0;` at the end of `send_key`). If
the user arms a modifier then backs all the way out to `VIEW_NORMAL`/`VIEW_HOME`
without visiting a key screen, `pending_mods` stays nonzero indefinitely — the *next*
key sent from *any* future hub visit would silently carry the stale modifier.

**Fix:** `show_view()` now clears `pending_mods` whenever transitioning to any view
with `v < VIEW_HUB` (i.e. Normal/Home/Media/Settings — anything outside the macro
hub itself). The intended arm→navigate-within-hub→send flow is unaffected since all
of Hub/F-keys/Numpad/Symbols/Modifiers are `>= VIEW_HUB`.

**Note for reviewer:** this is a logic-only fix, no threading/memory concerns;
lowest-risk item in the set. Confirmed by code inspection, not yet hardware-tested
(low priority given the mechanism is simple and provably correct by inspection).

### Issue 4 — Number-key corruption, varies boot-to-boot
See **§0 above** — this issue's fix was itself buggy and has just been corrected in
this same session/document. Do not treat any code shown earlier in the transcript for
this issue as current; only the block in §0 is final.

**Original symptom + diagnosis (still valid — only the fix implementation changed):**
numpad/F-key/symbol touches occasionally sent the wrong key, non-deterministically
across power cycles; volume/media touches were unaffected. Root cause:
`zmk_behavior_invoke_binding()` writes ZMK's shared keyboard HID report synchronously
on whichever thread calls it, and ZMK's HID/endpoints layer has no cross-thread lock
— by design, it assumes all key events originate from ZMK's own single-threaded
processing (its event manager / system workqueue). The touch UI originally invoked
key-sends directly from `ui_timer_cb()`, which runs on the **LVGL display thread** —
a second, uncoordinated writer to the same shared report, racing the dongle's normal
key-processing path. Consumer/media keys (`fire_macro` → `touch_macro_0..5`, e.g.
volume) write to ZMK's HID *consumer* report, a separate, far-less-contended
structure, which is why they never visibly corrupted.

### Issue 5 — Crash opening the Numpad screen
**Root cause:** `draw_cell()` allocates 2 LVGL objects per button (a container +
label). Every screen except the numpad is a 2×3 grid = 6 cells = ~12 objects. The
numpad is a 4×3 grid (`VIEW_NUMPAD` in `build_view()`, lines 212-225) = 12 cells =
~24 objects — roughly double. The OPERATOR layout's `LV_Z_MEM_POOL_SIZE` Kconfig
default is 20 KB (`prospector/.../layouts/operator/Kconfig.defconfig` line 13-14),
sized against the pre-existing (6-cell-max) screens; the numpad exceeds it.
`lv_obj_create()` returns `NULL` on allocation failure (LVGL 9 contract); the
pre-fix code unconditionally called `lv_obj_set_size(b, ...)` immediately after,
dereferencing NULL → hard fault.

**Fix, three parts, verified in this order via CI (32 KB attempt failed the linker
before the 25 KB attempt was tried — both build attempts are documented for the
record):**
1. `CONFIG_LV_Z_MEM_POOL_SIZE` in `prototype_mk1_waveshare.conf`: `20000` (default)
   → attempted `32768` → **CI linker failure**: `region 'RAM' overflowed by 4996 bytes`
   (exact message, from the build log) → corrected to **`25000`**, which built clean.
   This empirically establishes the RAM ceiling on this target: roughly 20000 + 7700
   ≈ 27.7 KB is the practical max for this Kconfig on this board at current feature
   set; 25000 leaves ~2.7 KB slack.
2. `draw_cell()` NULL-guards both allocations (`lv_obj_create` and `lv_label_create`)
   — on failure, returns early, skipping that one button rather than dereferencing
   NULL. This makes the *worst case* of any future pool exhaustion a **missing
   button**, never a crash, independent of the exact pool size chosen.
3. (Unrelated to the crash, landed in the same commit for convenience, then further
   corrected in §0): the key-send mechanism refactor.

**Explicitly flagged as NOT fully solved:** 25 KB was chosen to comfortably clear the
numpad's estimated ~3 KB extra requirement over the 20 KB baseline, with margin, but
was **not measured directly on hardware** (no LVGL memory-usage instrumentation was
added). If the numpad renders with one or more **blank buttons** (guard triggered,
no crash) rather than fully populated, the correct follow-up is a **shared
`lv_style_t`** applied to all buttons instead of the current per-object local styles
(`lv_obj_set_style_*` calls in `draw_cell` are all local/per-instance) — local styles
are known to have materially higher per-object memory cost in LVGL than a shared
style object referenced by every button. This was not implemented because it's a
larger structural change better done deliberately, not layered onto an
already-multi-fix session.

## §5. Fork commit history (prospector, `feat/new-status-screens`, chronological)
For bisecting or understanding intent evolution — each commit built green individually
except where noted:
```
f7781fa  touch navigation UI (initial 2-screen nav)
555d7b8  sub Back -> cell 1 (symmetry)
f81d9e7  uppercase UI labels (font tofu-box fix)
f4760d3  restyle (purple/black/rounded) + Settings screen w/ brightness+vol
0f863b2  Media screen macros (play/pause, prev, next)
6abbf3a  MACRO hub + F-keys
72081d9  raw-coord tap plumbing + 4x3 numpad support
5c8299e  Symbols + Modifiers screens
2392ac1  display brightness device-bind fix              (Issue 2)
3366d66  clear armed one-shot mod on hub exit              (Issue 3)
8ad5960  [SUPERSEDED — see §0] key-sends via zmk_behavior_queue_add (looked like Issue 4 fix, was not)
c1f730f  NULL-guard draw_cell + LVGL-pool-adjacent cleanup  (Issue 5, part of)
62ba9e5  [CURRENT] real k_work_submit thread-hop fix         (Issue 4, corrected)
```
Corresponding `dev/touch-screen` (main repo) commits bump `config/west.yml` 1:1 with
each fork commit above, plus the standalone `CONFIG_LV_Z_MEM_POOL_SIZE` commits
(`4ecdeb0` at 32768, reverted in intent by `750ee42` at 25000 — both are real commits
in history, `750ee42` is the one that stuck).

## §6. Verification status matrix

| Item | Compiles (CI) | Hardware-confirmed |
|---|---|---|
| Touch nav (Normal/Home/Media/Settings), 6-cell grid, 5s timeout | ✅ | ✅ (early session, before Issue 4 existed) |
| F-key screen key-send | ✅ | ✅ **but under the pre-Issue-4-fix code path** — i.e. confirmed keys transmit, NOT confirmed free of the corruption bug, since that bug is intermittent/boot-variant by nature |
| Numpad (4×3), Symbols, Modifiers screens | ✅ | ❌ never seen on hardware |
| Brightness device-bind fix | ✅ | ❌ |
| Reconnect deferred-subscribe (`central.c`) | ✅ (as of `c027b34`; §0's unrelated fork has no bearing on this) | ❌ — needs the sleep/wake cycle test in `CODE-REVIEW.md` §5 |
| Numpad crash fix (pool + guards) | ✅ | ❌ |
| §0 threading correction (this document) | ✅ confirmed green | ❌ |

**Net assessment: nothing from this entire session has been confirmed correct on
real hardware except the original touch-navigation skeleton and raw key transmission
(not corruption-freedom).** Every fix described here is a code-review-level fix,
justified by source-level reasoning against ZMK/Zephyr/LVGL internals, not by an
observed-fixed behavior on the device. Treat verification status as the single most
important thing to weight in a second review — logical soundness matters more here
than in a typical PR because empirical confirmation is largely absent.

## §7. Specific, pointed questions for the reviewer

1. Is the §0 claim about `k_work_submit()` (non-delayable) being an unconditional
   thread hop actually true in this Zephyr version, including from a workqueue that
   might already be draining another item? (i.e., can `k_work_submit` ever run the
   handler synchronously if called *from* the system workqueue thread itself, which
   would matter if anything else on that thread ever called into `queue_key`'s
   callers — currently nothing does, but worth confirming the primitive's contract.)
2. In `central.c`, is there any GATT discovery entry point *other than*
   `split_central_chrc_discovery_func`'s two exit branches that could leave a slot
   CONNECTED with some handles set and never call `flush_subscriptions`? (e.g. a
   disconnect mid-walk, an error path in `split_central_service_discovery_func`
   before the characteristic walk even starts.)
3. Does deferring the battery `bt_gatt_read()` change its relative ordering vs. the
   position-state subscribe in a way that could matter for the prospector battery
   widget's freshness/UX, independent of the correctness fix?
4. `send_key()`: `keycode | ((uint32_t)pending_mods << 24)` — verify this matches
   ZMK's actual mod-encoding convention in `dt-bindings/zmk/modifiers.h`
   (`APPLY_MODS` macro) bit-for-bit; the code was written against the header but
   never round-tripped through an actual `&kp LC(...)` comparison.
5. `cell_from_coords()` reads `grid_rows` (a plain, non-atomic global `int`) on the
   display thread while `build_view()` writes it on the same thread — confirmed
   single-thread here so no race, but flag if any other call path could touch
   `grid_rows` (there shouldn't be one; worth a second set of eyes).
6. Is 25 KB actually sufficient, or does §7 item and the "blank button" contingency
   in Issue 5 need to be treated as *expected*, not just possible? If so, is it worth
   proactively doing the shared-`lv_style_t` refactor now rather than waiting for a
   hardware report of missing buttons?

## §8. Pointers
- `CODE-REVIEW.md` §1–§9 — original per-change write-up (note: §8/§9 there describe
  the *pre-§0-correction* state of Issue 4; this document supersedes them for that
  specific issue only — everything else in `CODE-REVIEW.md` is still current).
- `ZMK-3156-DEEP-DIVE.md` — full reconnect root-cause research trail, including the
  rejected `BT_MAX_CONN` hypothesis and the buffer-math justification for why `=20`
  alone couldn't have been expected to fully fix it.
- `TOUCH-SCREEN-NOTES.md` — UI/UX design record, nav map, deferred swipe-to-back gesture.
