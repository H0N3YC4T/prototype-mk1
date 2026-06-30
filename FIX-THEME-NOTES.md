# dev/fix-theme — halves' gem themes don't pause / update on key presses

Investigation + fix notes. Question posed: *why don't the halves' themes pause or update on
key presses, and is it even possible given peripheral vs central restrictions?*

## Verdict on feasibility

| Want | Possible on a half (peripheral)? | Why |
|---|---|---|
| **Pause / cycle via the `&cycle_animation` hotkeys** | **Yes** — fixed here | Behaviour is GLOBAL-locality and relays to peripherals; only gap was the behaviour wasn't *registered* on the half |
| **Animation react to *this half's* own typing** | **Yes** (feasible, not yet done) | The peripheral raises `zmk_position_state_changed` locally (`split/peripheral.c` subscribes to it) |
| **WPM / typing speed on the half** | **No** | `ZMK_WPM` is selected for the central only (`Kconfig.defconfig`: `if !ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL`); no HID on a peripheral to measure |
| **React to the *other* half / whole-keyboard activity** | **No** (without a custom relay) | A peripheral only sees its own matrix; cross-half state lives on the central/dongle |

So your instinct is right: there *are* hard peripheral limits (no WPM, no whole-keyboard activity),
but the hotkey control and local-typing reactions are both achievable.

## Root cause of "hotkeys don't pause/update the halves"

The gem animation is a **free-running LVGL loop** (`animation.c` `draw_animation()` →
`lv_animimg`, `LV_ANIM_REPEAT_INFINITE`). It never reacts to keys by itself; the only control is
the `cycle_animation` behaviour (NVC_PAUSE/NEXT/PREV), handled by the gem listener
`nice_view_cycle_animation_listener` (`animation.c:152`), which exists on **both** the central and
peripheral screens.

`&cycle_animation` is `BEHAVIOR_LOCALITY_GLOBAL`, so when pressed on the dongle, ZMK
(`behavior.c:106`) calls `zmk_split_central_invoke_behavior(i, …)` for each peripheral — relaying
the behaviour **by name → local-id**. The peripheral then does
`zmk_behavior_get_binding("cycle_animation")` to run it.

**But the behaviour node is only defined in `prototype_mk1.keymap`'s `behaviors {}` block, and the
halves don't compile the keymap** (the peripheral overlays include `prototype_mk1.dtsi` for the
matrix/kscan only). So on the half, `zmk_behavior_get_binding` fails ("No behavior assigned"), the
relayed invocation is dropped, `cycle_animation_state_changed` is never raised on the half, and the
gem listener never fires → **the theme never pauses or cycles on the halves.** This is also why
the earlier transmutation-lock fix alone didn't make cycling work on the halves.

## Fix applied (this branch)

Register the behaviour on the peripherals so the relay can resolve it — added to
`prototype_mk1_periph_left.overlay` and `prototype_mk1_periph_right.overlay`:
```dts
/ { behaviors { cycle_animation: cycle_animation {
    compatible = "zmk,behavior-cycle-animation"; #binding-cells = <1>; }; }; };
```
No keymap edit, no duplication (the halves don't have the keymap's copy; `central_left` and the
dongle are untouched and still get theirs from the keymap). `behavior_cycle_animation.c` is already
compiled into every build (`CMakeLists.txt` `target_sources(app …)`, self-guarded on
`DT_HAS_COMPAT_STATUS_OKAY`), so adding the node activates it on the half.

**Expected result:** pressing `&cycle_animation NVC_PAUSE/NEXT/PREV` on the dongle now relays to
both halves; each half's gem pauses / cycles its theme. **Needs hardware confirmation.**

> CI cross-check: if a half *did* already compile the keymap, this node would be a duplicate label
> and the build would fail — so a green `left`/`right` build also confirms the diagnosis.

## Optional follow-up (separate, feasible): animate on local typing

To make a half's animation *react to its own typing* (e.g. resume/advance while typing, settle to
a static frame when idle), subscribe the gem to `zmk_position_state_changed` on the peripheral and
drive `nice_view_animation` + a short idle timer from it. This is self-contained to the gem and
needs no central relay. Not done yet — it's a design choice (what exactly should it do?), and it
only ever reflects *that half's* keys, never WPM or the other half.

## Status
- [x] Feasibility determined
- [x] Hotkey-relay bug found + fix applied (periph overlays)
- [ ] CI build of `left`/`right`
- [ ] Hardware confirmation (pause/cycle on halves)
- [ ] (Optional) local-typing animation reactivity — pending a design decision
