# ZMK #3156 — split central fails to discover peripheral position-state after battery fetch: deep dive

> Working research notes. Built incrementally during an autonomous deep-dive session.
> Topic: why the waveshare dongle's halves link but stop typing after sleep until the
> dongle is power-cycled, and the real fix (`CONFIG_BT_ATT_TX_COUNT`, ZMK #3156 / PR #3216).

## 0. TL;DR (current understanding)

- **Symptom:** half pairs fine on first connect; after it deep-sleeps and wakes, it
  re-establishes the BLE link (and battery may even show) but never re-subscribes to
  the key-position characteristic → no keys from that half until the **dongle** is reset.
- **Root cause (ZMK #3156):** with `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`
  on, the central, mid-GATT-discovery of a peripheral, subscribes to the BAS battery
  characteristic. That subscribe triggers a **nested CCC-descriptor discovery** in
  Zephyr's GATT client, which collides with the *in-progress* ZMK discovery walk and
  ends it prematurely → position-state characteristic never found/subscribed.
- **Why it presents as a buffer issue:** the concurrent discovery/subscribe work needs
  **ATT TX buffers**. On older Zephyr ZMK sized this via `BT_L2CAP_TX_BUF_COUNT`; on
  newer Zephyr (4.1, ours) ATT has its own pool controlled by **`BT_ATT_TX_COUNT`**, so
  the old knob no longer helped and the pool was too small → discovery starved.
- **Official fix:** ZMK PR #3216 (merged 2026-02-18) — `BT_ATT_TX_COUNT default 10 if
  ZMK_SPLIT_ROLE_CENTRAL`. Present in our pinned zmk `64daf698` (pinned 2026-06-23).
- **Our change:** `CONFIG_BT_ATT_TX_COUNT=20` in `prototype_mk1_waveshare.conf` (margin
  for 2 peripherals + battery fetch + prospector + 3 host profiles).
- **Open questions to resolve in this dive:** (a) is the abort a *buffer* exhaustion or
  a *discovery-state* collision, or both? (b) exactly how many ATT TX buffers does our
  worst case need? (c) is 20 right, too low, or overkill? (d) are there code-level or
  ordering fixes more robust than just enlarging the pool?

## 1. Established facts (sourced)

| Fact | Source |
|---|---|
| Issue is dongle + ≥2 peripherals + `BATTERY_LEVEL_FETCHING` | ZMK #3156 |
| Log signature: `Discover complete` without `Found position state characteristic` | ZMK #3156 |
| Disabling battery fetching makes position-state discovery reliable | ZMK #3156 |
| Battery subscribe starts a nested CCC discovery that "might mess up the ongoing discovery and cause it to end prematurely" | ZMK #3156 maintainer comment |
| Fix PR #3216 merged 2026-02-18, commit `9490391` | GitHub |
| PR #3216 change: remove `BT_L2CAP_TX_BUF_COUNT default 5 if central`; add `BT_ATT_TX_COUNT default 10 if central` in `app/src/split/bluetooth/Kconfig` | commit `9490391` |
| Our pinned zmk `64daf698` dated 2026-06-20, pinned in west.yml 2026-06-23 → includes the fix | git + GitHub API |

Sources: ZMK issue #3156, ZMK PR #3216 / commit 9490391.

## 2. To investigate (running checklist)

- [ ] Full ZMK central discovery state machine (`central.c`) — exact order of operations
- [ ] How `split_central_subscribe` works + whether it issues its own discovery
- [ ] Zephyr `bt_gatt_subscribe` + CCC auto-discovery behaviour
- [ ] Zephyr ATT TX buffer architecture (`BT_ATT_TX_COUNT`) vs L2CAP/ACL pools
- [ ] Buffer-demand math for our worst case (2 peripherals reconnecting at once)
- [ ] Why "first connect OK, post-sleep broken" (timing/concurrency)
- [ ] Related issues (#3207, #3095, #718, #764) cross-refs
- [ ] Alternative/defensive fixes (ordering, deferring battery subscribe, config)
- [ ] Verification plan + what to capture in a USB log

---

## 3. Findings (appended as work proceeds)

### 3.1 ZMK central discovery mechanism (`app/src/split/bluetooth/central.c`)

Reconnect flow on the central:
1. Peripheral re-advertises (directed) → central scans, `reserve_peripheral_slot()`,
   `bt_conn_le_create` → `connected` → security elevates → `split_central_process_connection()`.
2. `split_central_process_connection()` (l.748) starts a **PRIMARY service discovery**
   for the ZMK split service (`slot->discover_params`, 0x0001–0xffff).
3. `split_central_service_discovery_func` (l.713) finds the split service then kicks off a
   **CHARACTERISTIC discovery** walk handled by `split_central_chrc_discovery_func` (l.538).
4. That single callback walks characteristics and, **inline as it matches each one**, fires
   the subscription immediately:
   - position-state (l.562) → `split_central_subscribe(&slot->subscribe_params)`
   - run-behavior (l.607), select-physical-layout (l.612), HID-indicators (l.618) → store handle
   - **battery (BAS 0x180F, l.623–638)** → `split_central_subscribe(&slot->batt_lvl_subscribe_params)`
     **AND** `bt_gatt_read(&slot->batt_lvl_read_params)` — i.e. an *extra subscribe + a read*.
5. The walk only stops (`subscribed`, l.688) once position-state + run-behavior + layout
   (+ battery when fetching) handles are all set.

`split_central_subscribe` (l.480): `atomic_set(flags, BT_GATT_SUBSCRIBE_FLAG_NO_RESUB)` then
`bt_gatt_subscribe(conn, params)`. Each subscribe carries `params->disc_params =
&slot->sub_discover_params` — **a single shared CCC discover-params struct** reused across the
position-state and battery subscribes on that slot.

**Why battery fetching breaks it:** `bt_gatt_subscribe` must write the CCC descriptor; when the
CCC handle isn't known it **auto-discovers the CCC** (a *nested* `bt_gatt_discover`) using
`disc_params`. With fetching on, the inline battery subscribe + read fire **mid-walk**, so the
connection now has the main characteristic discovery **plus** nested CCC discoveries **plus** a
battery read all in flight at once. Two failure surfaces:
- **Shared `sub_discover_params`:** position-state and battery subscribes reuse the same CCC
  discover-params struct → a second nested CCC discovery can stomp the first.
- **ATT TX buffer/req contention:** every concurrent discover/subscribe/read needs an ATT TX
  buffer + request slot; exhausting the pool makes an op fail and the walk terminate early
  → `Discover complete` *without* `Found position state characteristic` → no key subscription.

This matches the maintainer note exactly ("subscribing to battery starts another discovery that
messes up the ongoing one and ends it prematurely"). The official fix targets the **buffer**
surface (`BT_ATT_TX_COUNT`), which is the cheap/robust lever; the shared-`sub_discover_params`
surface is mitigated because more buffers let the ops complete before they collide.

### 3.2 Zephyr ATT buffer architecture (the real "why")

From Zephyr `subsys/bluetooth/host/Kconfig.gatt` (main):
```
config BT_ATT_TX_COUNT
    int "Number of ATT buffers"
    default BT_BUF_ACL_TX_COUNT
    default 3
    range 1 255
    help: These buffers are only used for sending anything over ATT.
          Requests, responses, indications, confirmations, notifications.
config BT_GATT_AUTO_DISCOVER_CCC
    bool "Support to automatic discover the CCC handles of characteristics"
    depends on BT_GATT_CLIENT
    help: ...initiate discovery for CCC handles if the CCC handle is unknown by the application.
```

Critical points:
- **`BT_ATT_TX_COUNT` is ONE shared pool for every ATT TX PDU on every connection** — peripheral
  discovery requests, CCC auto-discovery requests, CCC writes, battery reads, AND the HID
  notifications the dongle sends to its host. It is not per-connection.
- **Default is 3** (`BT_BUF_ACL_TX_COUNT`, which is 3 unless HCI_RAW/MESH). We set nothing for
  `BT_BUF_ACL_TX_COUNT`, so a plain central gets **3 ATT buffers**.
- `BT_GATT_AUTO_DISCOVER_CCC` is what makes `bt_gatt_subscribe` fire the nested CCC discovery
  seen in 3.1. Each such auto-discovery is another ATT request drawing from the same pool of 3.

**Why the OLD knob stopped working (the essence of PR #3216):** older Zephyr fed ATT from the
L2CAP TX pool, so ZMK bumping `BT_L2CAP_TX_BUF_COUNT=5` raised the ATT budget. Newer Zephyr
(4.x / our 4.1) gave ATT its **own** pool (`BT_ATT_TX_COUNT`, default 3) decoupled from L2CAP, so
the old override no longer touched ATT → discovery silently ran on 3 buffers again → #3156.

### 3.3 Buffer-demand math (our worst case) — is 20 right?

ATT allows **one outstanding *request* per connection**, so per-connection requests serialize.
But the pool also covers writes-without-response, notifications, reads, and confirmations, and is
**shared across all links**. Rough peak during a simultaneous 2-peripheral reconnect with battery
fetching, while the host link is active:
- 2 peripheral characteristic-discovery requests in flight ............ ~2
- their position-state CCC auto-discover + CCC write .................. ~2–4
- their battery CCC auto-discover + CCC write + battery read .......... ~2–6
- physical-layout write-without-response per peripheral ............... ~2
- HID key-report notifications to the host during all this ............ ~2–4
→ transient peak comfortably in the **10–14** range.

So:
- **3 (stock default):** starves immediately → #3156. ✓ explains the bug.
- **10 (ZMK PR #3216 default):** covers the typical case; can be tight at the very peak for a
  2-peripheral + battery + active-host burst.
- **20 (our `prototype_mk1_waveshare.conf`):** ~2x margin over the estimated peak. Justified, not
  arbitrary; RAM cost ≈ 20 × ~(BT_L2CAP_TX_MTU+hdr) ≈ low single-digit KB on the XIAO's 256 KB.
- Note: `BT_ATT_TX_COUNT` defaults *from* `BT_BUF_ACL_TX_COUNT` but is independent once set; the
  official fix bumps **only** ATT (not ACL), which confirms ATT is the binding constraint. We do
  the same — no need to also raise `BT_BUF_ACL_TX_COUNT`.

Sources: Zephyr `Kconfig.gatt` (BT_ATT_TX_COUNT, BT_GATT_AUTO_DISCOVER_CCC); ZMK PR #3216.

### 3.4 The precise failure (from the #3156 maintainer analysis)

The root-cause comment on #3156 is by **carrefinho — the author of the prospector module the
dongle runs**. Their trace:
- `bt_gatt_discover` fails with **-12 (`-ENOMEM`)** — i.e. it ran out of ATT buffers.
- It happens because a **CCC discovery starts (at e.g. handle 0x0012) while ZMK's characteristic
  discovery is still in progress** → resource contention in Zephyr's `gatt_discover_next`
  (`subsys/bluetooth/host/gatt.c`).
- **The killer detail:** when `bt_gatt_discover` errors, Zephyr's error path calls
  `params->func(conn, NULL, params)` — i.e. it invokes the discovery callback with `attr == NULL`,
  which is the *same* signal Zephyr uses for "discovery finished normally". ZMK's
  `split_central_chrc_discovery_func` can't tell the difference: `if (!attr) { LOG_DBG("Discover
  complete"); return BT_GATT_ITER_STOP; }` (central.c l.541). **So an ENOMEM failure is silently
  reported as a successful, complete discovery.** Position-state was never reached → never
  subscribed → that half connects but sends no keys.

This nails the exact log signature people see: `Discover complete` with **no** `Found position
state characteristic`, and (at DBG) a CCC discovery starting mid-walk + a `-12`.

### 3.5 Two fixes: the merged buffer bump vs the unmerged code fix

1. **Merged (PR #3216): enlarge the pool.** `BT_ATT_TX_COUNT 3 → 10` for centrals. More buffers
   ⇒ the concurrent CCC discovery no longer hits ENOMEM ⇒ the walk finishes for real. This is a
   *mitigation*: it removes the trigger (buffer starvation) but leaves the underlying design
   (subscribing inline, mid-discovery) intact. A heavy enough setup can still peak past the pool.
2. **Proposed but NOT merged (carrefinho): fix the ordering.** "Defer subscribing to
   characteristics until after the entire characteristic discovery completes, instead of
   subscribing immediately when a characteristic is found." This removes the *nested* discovery
   entirely (no CCC discovery runs during the char walk) → no contention regardless of buffer
   count. More robust, but requires patching ZMK `central.c` (we'd have to vendor/fork it).

**Implication for us:** we're on the merged buffer fix (10) + our extra margin (20). That should
be plenty for 2 peripherals. If — and only if — 20 still proves insufficient on hardware, the
escalation is the ordering fix (fork `central.c` to defer subscribes), not yet-bigger buffers.

### 3.6 Why "first connect OK, breaks after sleep, stuck until reset"

- **First connect (cold boot):** the central finds and connects to peripherals largely
  **sequentially** (it `stop_scanning()`s to connect, discovers, then resumes). Low concurrency
  ⇒ peak ATT-buffer demand stays under even the stock 3 ⇒ discovery succeeds.
- **After sleep → reconnect:** a half wakes and reconnects **while the host link is active and/or
  the other half is also re-discovering** ⇒ higher concurrency ⇒ peak exceeds 3 ⇒ ENOMEM ⇒
  discovery "completes" without subscribing.
- **Stuck (not self-healing):** because the failure masquerades as a *successful* completion, ZMK
  does **not** retry discovery — the slot sits CONNECTED-but-unsubscribed. Nothing re-triggers a
  fresh discovery until the link drops. **Resetting the dongle** forces a clean, sequential
  re-discovery (buffers free) → works again. That's exactly the reported behaviour, and it's why
  a *central* reset (not just a peripheral reset) reliably clears it.

The buffer fix breaks this loop at the source: with 10/20 buffers the reconnect discovery never
hits ENOMEM, so it never gets stuck.

**Refinement on the trigger:** the real trigger is **concurrent discovery of both peripherals at
the same time** — which happens on a reconnect, but *also* on a cold boot if both halves are
already powered when the dongle starts (it then connects to both "at around the same time"). So
"first connect was fine" likely means the halves were brought up in sequence that time; after
sleep they wake together. The buffer size, not the trigger timing, is what we control.

### 3.7 Related issues (context, not our bug)

- **#3095** — compile error when reporting battery for **3+ parts** (dongle + 2-half split). We
  have exactly 2 peripherals so we don't hit the compile cap, but it's the same battery-fetch
  area; relevant if a 3rd peripheral were ever added.
- **#3207** — central/master half fails to wake from deep sleep + drains battery (regression on
  `main` vs v0.3). Not our path (our dongle is mains/USB-powered and `CONFIG_ZMK_SLEEP=n`), but
  it's why keeping the dongle from ever deep-sleeping is the right call independent of #3156.
- **#718** — generic "peripheral randomly disconnects and doesn't auto-reconnect" — older split
  reconnection bucket; #3156 is the specific battery-fetch instance of "doesn't reconnect right".
- **#764** — the feature issue that introduced split battery reporting/fetching over GATT (the
  feature whose subscribe is the trigger here).

### 3.8 Verification plan (how to prove it on hardware)

1. **Flash the current `dongle-waveshare`** (now carries `BT_ATT_TX_COUNT=20`) + current halves.
2. **Reproduce the trigger:** leave the keyboard idle until both halves deep-sleep (≥20 min, the
   `IDLE_SLEEP_TIMEOUT=1200000`), then wake both ~together and type on each half. Repeat ~5–10
   sleep/wake cycles (the old failure accumulated over cycles).
3. **For a definitive read, capture the dongle serial log:** temporarily build the dongle with
   `CONFIG_ZMK_USB_LOGGING=y` (it's already present, commented, in `prototype_mk1_waveshare`-side
   confs / `prototype_mk1_dongle.conf:42`) and watch USB CDC at reconnect:
   - **Healthy:** `Found position state characteristic` **and** `[SUBSCRIBED]` for *each* half.
   - **Still starved:** `Discover complete` with **no** `Found position state characteristic`,
     and at DBG a CCC discovery starting mid-walk and/or `bt_gatt_discover ... (err -12)`.
   Remove `CONFIG_ZMK_USB_LOGGING` again for daily firmware (it adds overhead).

### 3.9 Recommendations / decision tree

1. **Do first:** flash the current build and run the cycle test above. Because the official fix
   (`=10`) has been in our pinned zmk since 2026-06-23, there's a real chance the issue was only
   ever seen on pre-June-23 firmware and is already gone; our `=20` just adds margin.
2. **If it still recurs at 20** (confirmed via the log showing the `-12`/early-complete pattern):
   do **not** keep raising buffers blindly. Escalate to the **code fix** — fork ZMK `central.c`
   to *defer all `split_central_subscribe()` calls until the characteristic-discovery walk
   completes* (carrefinho's proposal in 3.5). That removes the nested-discovery contention
   entirely. (Same fork pattern we already use for the prospector module.)
3. **Keep** `CONFIG_ZMK_SLEEP=n` on the dongle — unrelated to #3156, but correct hygiene and it
   sidesteps the separate #3207 central-sleep regression.
4. **Do not** reintroduce `BT_MAX_CONN`/`BT_L2CAP_TX_BUF_COUNT` overrides for this — they don't
   touch the ATT pool on Zephyr 4.1 and were a misdiagnosis.

### 3.10 Status of the checklist

- [x] Full ZMK central discovery state machine — see 3.1
- [x] `split_central_subscribe` + nested CCC discovery — see 3.1, 3.4
- [x] Zephyr `bt_gatt_subscribe` / `BT_GATT_AUTO_DISCOVER_CCC` — see 3.2, 3.4
- [x] Zephyr ATT TX buffer architecture (`BT_ATT_TX_COUNT`) — see 3.2
- [x] Buffer-demand math / is 20 right — see 3.3
- [x] Why first-OK / post-sleep-stuck — see 3.6
- [x] Related issues — see 3.7
- [x] Alternative/defensive fixes — see 3.5, 3.9
- [x] Verification plan + log lines — see 3.8
- [ ] HARDWARE CONFIRMATION — pending the user's cycle test (the one thing notes can't settle)

---

## Appendix A — Escalation patch sketch (deferred-subscribe), only if 20 buffers still fail

Target: fork `zmkfirmware/zmk`, edit `app/src/split/bluetooth/central.c`
(`split_central_chrc_discovery_func`), pin `config/west.yml` to the fork commit (same pattern as
the prospector fork). Goal: never run a CCC discovery *while* the characteristic walk is in
flight.

Change shape:
1. **During** the `BT_GATT_DISCOVER_CHARACTERISTIC` walk, for each matched characteristic only
   **record the value handle** into the slot — do **not** call `split_central_subscribe()` /
   `bt_gatt_read()` inline. Affected matches today: position-state (l.562), sensor (l.572),
   input (l.585), battery (l.623). (run-behavior/layout/hid already only store handles.)
2. Let the walk run to its real end. In the **`if (!attr)` "Discover complete" branch** (l.541),
   *after* confirming the walk wasn't an error (see note), issue the subscribes **sequentially**:
   position-state → sensor → input → battery `bt_gatt_subscribe`, then the battery `bt_gatt_read`.
   ATT's one-outstanding-request rule then serialises each subscribe's CCC discovery so none
   overlaps another or the (now-finished) main walk.
3. Give each subscribe its **own** `disc_params` (today position-state and battery share
   `slot->sub_discover_params`); or, since they're now sequential, reuse is safe because each
   completes before the next starts.

Bonus hardening (separate, also worth upstreaming): ZMK can't currently distinguish "discovery
finished" from "discovery failed (`-ENOMEM`)" because both arrive as `attr == NULL` (see 3.4).
Tracking an explicit "did we find position-state?" flag and **retrying discovery** when the walk
ends without it would make the firmware self-heal instead of sitting CONNECTED-but-unsubscribed
until a reset — fixing the *symptom* (stuck) even when the *trigger* (buffers) recurs.

This is documented as a ready option; it is **not applied** — the merged buffer fix + our `=20`
margin should make it unnecessary. Apply only if 3.8's log shows the `-12`/early-complete pattern
persists at 20.

## Appendix B — exact config state after this work

`boards/shields/prototype_mk1/prototype_mk1_waveshare.conf` (dongle):
```
CONFIG_ZMK_SLEEP=n            # hygiene; not the #3156 fix
CONFIG_BT_ATT_TX_COUNT=20     # the #3156 fix (ZMK default 10 for a central; 20 = margin)
```
Pinned zmk `64daf698` already provides `BT_ATT_TX_COUNT default 10 if ZMK_SPLIT_ROLE_CENTRAL`
(PR #3216). Battery fetching stays on (prospector `Kconfig.defconfig` default y). 3 BT profiles
unchanged (`BT_MAX_PAIRED=5`).

## Sources
- ZMK issue #3156 — https://github.com/zmkfirmware/zmk/issues/3156 (root-cause comment by carrefinho)
- ZMK PR #3216 / commit 9490391 — the `BT_ATT_TX_COUNT` buffer fix
- Zephyr `subsys/bluetooth/host/Kconfig.gatt` — `BT_ATT_TX_COUNT`, `BT_GATT_AUTO_DISCOVER_CCC`
- Related: ZMK #3095, #3207, #718, #764

