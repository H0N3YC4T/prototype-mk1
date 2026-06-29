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

