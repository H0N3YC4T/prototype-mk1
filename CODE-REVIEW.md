# Code review — dongle touch UI + brightness & reconnect fixes

Branch: `dev/touch-screen`. Reviewer pass covering everything built in this arc:
the two-screen touch UI + MACRO hub, plus the two hardware-reported bugs
(display brightness, split reconnect). Forks involved:
- `H0N3YC4T/prospector-zmk-module` @ `feat/new-status-screens` (display UI + brightness)
- `H0N3YC4T/zmk` @ `fix/3156-deferred-subscribe` (split reconnect) — **new this pass**

`config/west.yml` pins both; each iteration bumped the pin so CI builds are reproducible.

---

## 1. What changed (by area)

### Touch UI (prospector fork `status_screen.c`, our `touch_input.c` + overlay)
- Full-screen `overlay` object, hidden until a tap; an `lv_timer` (30 ms, display
  thread) drains taps and handles the idle timeout. Touch callback (workqueue
  thread) only does `atomic_set` — **no LVGL off-thread** (the one hard rule here).
- Navigation: NORMAL → HOME → { MEDIA, SETTINGS, MACRO hub }. Hub → { F-keys,
  Numpad, Symbols, Modifiers }.
- Key sends reuse ZMK's `&kp` (`behavior_dev="key_press"`, `param1 = keycode |
  (pending_mods<<24)`) — no per-key macros. One-shot modifiers persist across
  navigation, consumed by the next key *sent*.
- Grid is per-screen: `touch_input.c` now passes **raw screen coords**; the fork
  maps to a cell via `grid_rows` (2, or 4 for the numpad) + `cell_from_coords()`.
- Media/volume fire `touch_macro_0..5` behaviours defined in the overlay.

### Bug 1 — display brightness (prospector fork `brightness.c`)
- Root cause + fix in §2.

### Bug 2 — split reconnect after sleep (zmk fork `central.c`)
- Root cause + fix in §3.

---

## 2. Bug 1: display brightness never changed

**Symptom:** the Settings brightness ∓ buttons did nothing; the panel stayed at
one level.

**Root cause:** this build has **two** `pwm-leds` devicetree nodes —
1. `disp_bl` (`&pwm1`, P1.11) — the real display backlight, from the prospector
   adapter's `xiao_ble_zmk.overlay`;
2. `backlight`/`pwm_led_0` (`&pwm0`, P0.08) — a keyboard-backlight **relay
   placeholder** our `prototype_mk1_waveshare.overlay` adds so `CONFIG_ZMK_BACKLIGHT`
   + the GLOBAL `&bl` behaviour can relay to the halves (P0.08 is an unconnected pad).

`brightness.c` bound the controller with `DEVICE_DT_GET_ONE(pwm_leds)`, which is
ambiguous with two instances — it could (and did) resolve to the **placeholder**,
so every `led_set_brightness()` (both the boot fixed-brightness `SYS_INIT` *and* the
touch stepper) drove a dead pad. The display sat at its power-on default; nothing
the UI did moved it. Nobody noticed before because there was no brightness control
until the Settings screen added one.

**Fix:** bind the display's controller **specifically** via `disp_bl`'s parent:
```c
static const struct device *pwm_leds_dev = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(disp_bl)));
```
`DISP_BL` (`DT_NODE_CHILD_IDX(disp_bl)`) is unchanged (still 0). This is immune to
any number of other `pwm-leds` nodes. Also added a `LOG_INF("display brightness ->
N%% (delta, rc)")` so the USB serial log confirms the call + `led_set_brightness`
return code on hardware.

**Why it's correct / low-risk:** the parent of `disp_bl` is the very node the
pwm-leds driver instantiates as a device; `DEVICE_DT_GET` on it is exact. On stock
prospector (one `pwm-leds`) this resolves identically to the old code, so it can't
regress upstream behaviour.

**Verification:** compiles green (fork `2392ac1`). Hardware: flash → Settings ∓
should move the panel and print the log line. If it still doesn't move, the log's
`rc` disambiguates driver failure vs. wiring (BL not actually on P1.11).

---

## 3. Bug 2: halves link after sleep but send no keys (ZMK #3156)

**Symptom:** after a half deep-sleeps and reconnects, it re-establishes the BLE
link but never resubscribes to key positions — no keys from that half until the
**dongle** is power-cycled. Persisted even with `CONFIG_BT_ATT_TX_COUNT=20`.

**Root cause (confirmed applied, still insufficient):** covered exhaustively in
[ZMK-3156-DEEP-DIVE.md](ZMK-3156-DEEP-DIVE.md). In `central.c`, the peripheral
characteristic-discovery walk subscribes to each characteristic **inline, mid-walk**.
The battery subscribe (and position-state) each kick off a nested CCC auto-discovery
that overlaps the in-flight walk. Under reconnect load that (a) contends for the
shared ATT buffer pool and (b) both CCC discoveries reuse **one** `sub_discover_params`
struct. When a nested discovery fails with `-ENOMEM`, Zephyr's `gatt_discover_next`
reports it to the callback as `attr == NULL` — **identical to a clean completion** —
so ZMK thinks discovery finished, never subscribes position-state, and gets stuck
(the failure masquerades as success, so nothing retries).

`CONFIG_BT_ATT_TX_COUNT=20` widens the buffer pool (surface *a*) and is genuinely
applied (verified: the global `config/prototype_mk1.conf` doesn't override it, and
`dongle-waveshare` builds `prototype_mk1_waveshare`). That it still fails points at
surface *b* / the mid-walk overlap itself — which buffers alone can't remove.

**Fix (zmk fork `central.c`, +46/-6):** defer every subscription out of the walk.
- During the walk, only **record** handles into the subscribe-params (as before) —
  removed the inline `split_central_subscribe()` for position-state, sensor, and
  battery, and the inline battery `bt_gatt_read()`.
- New `split_central_flush_subscriptions()` issues them **after** the walk stops,
  sequentially. Called at both walk-exit points (they're mutually exclusive per
  walk): the `subscribed`-true early stop, and the `attr == NULL` natural completion.
- Battery gets its **own** `batt_lvl_sub_discover_params` so its CCC discovery can't
  stomp position-state's shared struct.

No CCC discovery now runs while the walk is in flight; ATT serialises the sequential
post-walk subscribes. This removes the contention at the source rather than widening
the pool (the maintainer's own proposed fix on #3156).

**Why it's correct:** the `subscribed` gate still keys off `value_handle` (set during
the walk), so the walk stops at the same point as before — only the subscribe call
moves after it. Normal cold-boot path traced: handles recorded → `subscribed` true →
flush subscribes in order → STOP. Peripheral missing a characteristic → walk runs to
`attr==NULL` → flush subscribes whatever was found. `bt_gatt_subscribe` returns
`-EALREADY` if re-entered, so flush is safe to call once per walk.

**Risk:** this is core split BLE code — the highest-risk change in the set. A defect
would present as *no* peripheral input (not intermittent), which is obvious and fully
reverted by pointing `west.yml`'s zmk back at `zmkfirmware` @ `64daf698`. **Must be
hardware-verified** with a sleep/wake cycle test (see §5).

**Self-heal (deliberately not added):** the chosen option mentioned a retry. The
silent failure sets `value_handle` even when the CCC write never lands, so it can't
be detected by the existing `!value_handle` check without tracking real notification
receipt — non-trivial and risky. The deferred-subscribe fix removes the failure mode,
making a retry unnecessary; left as a documented escalation if hardware still shows it.

---

## 4. File-by-file review notes

| File | Notes |
|---|---|
| `status_screen.c` (fork) | Thread split correct (atomic in, LVGL on timer). Coord pack fits 10+10+flag bits < int. `cell_from_coords` clamps col/row. `grid_rows` set in `build_view`, read in `cell_from_coords` — same (display) thread. Timeout gated `cur_view < VIEW_HUB`. ✓ |
| `touch_input.c` | Hook now `(sx,sy)`; fork strong-symbol + weak fallback (`touch_cell` 2×3) both updated. Rotation transform unchanged (HW-confirmed earlier). ✓ |
| `brightness.c` (fork) | Device-bind fix (§2). Stepper clamps 5–100. ✓ |
| `central.c` (zmk fork) | Deferred-subscribe (§3). Flush guarded per subscribe on `value_handle`. ✓ pending HW. |
| `prototype_mk1_waveshare.overlay` | Two `pwm-leds` nodes are *intentional* (display + keyboard-relay placeholder); the ambiguity they caused is now handled in `brightness.c`, not by removing a node. CST816S on `&i2c1` (D4/D5/D0/D1); no pin clash with P1.11 backlight or P0.08 placeholder. ✓ |
| `prototype_mk1_waveshare.conf` | `BT_ATT_TX_COUNT=20` retained as belt-and-suspenders alongside the code fix. `USE_AMBIENT_LIGHT_SENSOR=n` → `als_thread` not compiled, so nothing overrides the touch brightness. ✓ |
| `config/west.yml` | zmk → honeycat fork `c027b34`; prospector → `2392ac1`; dongle-display unchanged. (Earlier slip where a bulk-replace hit the *first* `revision:` was caught and corrected; bumps now target the exact SHA.) ✓ |

---

## 5. Verification status

| Item | Build (CI) | Hardware |
|---|---|---|
| Touch UI (Media/Settings/hub/F-keys/numpad/symbols/mods) | ✅ green | ✅ keys type; nav/timeout confirmed |
| Brightness device-bind fix | ✅ green | ⏳ flash & confirm ∓ moves panel (+ serial log) |
| Reconnect deferred-subscribe (zmk fork) | ✅ green (all central targets) | ⏳ **sleep/wake cycle test required** |

**Reconnect HW test:** flash all three parts from `dev/touch-screen`; idle until
both halves deep-sleep (~20 min), wake together, type on each. Repeat ~5–10 cycles.
Healthy = both halves keep working with no dongle reset. USB logging is on
(`CONFIG_ZMK_USB_LOGGING=y`) — a healthy reconnect shows `Found position state
characteristic` + `[SUBSCRIBED]` per half; the old failure showed `Discover complete`
without them.

## 6. Open / deferred
- **Swipe-to-back** gesture (top-to-bottom) — wishlist, needs swipe detection in the
  tap-only `touch_input.c`. See `TOUCH-SCREEN-NOTES.md`.
- **Self-heal retry** on the reconnect path — only if the deferred-subscribe fix
  proves insufficient on hardware (see §3).
- Everything is on `dev/touch-screen`, unmerged. The zmk fork is a new maintenance
  surface — on future zmk pin bumps, rebase `fix/3156-deferred-subscribe` onto the
  new base (or drop it if the fix lands upstream).

---

## 7. Independent deep-dive audit (2026-07-02)

Second pass after the two fixes, looking for anything else that could bite during
hardware testing. No new *bugs* found beyond one modifier footgun (fixed below);
the rest is confirmations + documented limitations so testing isn't a surprise.

### Sanity check — why reconnect "wasn't an issue in 0.3"
Confirmed the diagnosis holds and *sharpens* it. "0.3" is the version we're **on**
(ZMK v0.3.0 / Zephyr `v4.1.0+zmk-fixes`). Our tree shows ZMK now sets
`BT_ATT_TX_COUNT default 10` and has **zero** `L2CAP_TX_BUF_COUNT` references — the old
knob that fed ATT is gone. On pre-4.1 Zephyr, ZMK's L2CAP override gave centrals ~5
ATT buffers, enough that the mid-walk subscribe collision never starved; Zephyr 4.1
gave ATT its own default-3 pool, exposing #3156 (opened 2025-12-20). **Key inference:**
our `=20` (≫ the historical ~5) still fails, so buffer count is *not* the binding
constraint — the concurrency is. That's precisely what the deferred-subscribe patch
removes, validating it over any further buffer bump.

### Fixed
- **Armed one-shot modifier lingering past the hub.** A mod armed on the Modifiers
  screen sets `pending_mods` until a key is sent. If you left the hub (back to Normal)
  without sending a key, it persisted and the next key typed *anywhere* would carry it
  (armed Ctrl → later a stray Ctrl+A). Now `show_view()` clears `pending_mods` on any
  transition to a non-hub view (`v < VIEW_HUB`). The intended arm→navigate→key flow
  stays entirely inside the hub, so it's unaffected. (fork `3366d66`)

### Audited and ruled OUT
- **#3234 (Zephyr-4.1 stopped auto-enabling `CONFIG_BT`/`ZMK_BLE`/`ZMK_SPLIT_BLE`/
  `ZMK_USB`).** Our firmware demonstrably does BLE-split + USB HID (CI green, keys type
  over BLE), i.e. it is not in the "~90 KB, BLE stripped" broken state that regression
  produces. Our June pin post-dates the Feb-2026 regression window. Not affected.
- **`BT_MAX_CONN=5` on the waveshare dongle** (vs 7 on the nano dongle). Max
  *simultaneous* connections here = 2 halves + 1 host = 3; 5 leaves margin even across
  a reconnect. Not a bottleneck (and the user correctly rejected BT_MAX_CONN as the
  reconnect cause). Left as-is.
- **Two `&i2c1` declarations** (our `cst816s` + the adapter's `apds9960`). I2C is
  multi-drop; distinct addresses (0x15 vs 0x39); the apds9960 init failure (no sensor)
  is per-device and doesn't disable the bus — touch works. Fine.
- **`central.c` patch re-traced** end-to-end (cold boot, 2-peripheral reconnect,
  peripheral-missing-a-characteristic). Sound; also note the flush subscribes
  **position-state first**, so keys work even if the battery subscribe later contends.

### Known limitations / notes (not bugs — for awareness)
- **`CONFIG_ZMK_USB_LOGGING=y`** is kept on the dongle *on purpose* — it's needed to
  capture the reconnect serial-log signature. Remove it for daily firmware once the
  reconnect is confirmed (it adds a USB CDC ACM + logging overhead).
- **Brightness fix assumes the backlight is on P1.11** (`disp_bl`, per the adapter's
  DT). If the panel still doesn't dim, the added `LOG_INF` disambiguates: `rc 0` +
  no visible change ⇒ the module's BL isn't actually wired to that PWM (hardware), not
  software.
- **Input-split subscribe left inline** (not deferred). It's compiled out for us
  (`CONFIG_ZMK_INPUT_SPLIT` off — no peripheral pointing devices). If ever enabled, it
  would need the same deferral as position-state/battery.
- **Numpad (4×3) + Symbols + Modifiers screens are hardware-untested** — key-send is
  proven (F-keys type), but the 4×3 touch mapping and symbol/label rendering haven't
  been seen on the panel. Watch for row mis-mapping on the numpad (calibration, not a
  logic bug) and any tofu glyphs (montserrat_20 covers all 32 symbols, so unlikely).
- **Rapid taps:** the tap hand-off is a single atomic slot drained every 30 ms, so two
  taps inside one 30 ms window would drop the earlier one. Irrelevant at human tap
  rates; noted for completeness.
