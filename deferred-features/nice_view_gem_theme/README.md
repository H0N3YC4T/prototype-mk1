# Deferred: nice!view_gem custom theme system

Preserved during the ZMK Zephyr 4.1 migration. The `nice_view_gem` shield was
modularized to **stock M165437/nice-view-gem** (LVGL 9) to get the migration
done cleanly. This directory holds the custom theme work that was layered on the
old vendored copy, to be **rebuilt on the 4.x APIs** and wired to the
`cycle_animation` behavior afterward (the behavior is dormant — see
`app/src/behaviors/behavior_cycle_animation.c` and `include/zmk/...`).

These files are the **old LVGL 8** versions — they must be migrated to LVGL 9
when reinstated (see the "LVGL 8 → 9.3 translation pattern" in
`../../MIGRATION_NOTES.md`). Full pre-modularization snapshot is in git history
on `upgrade/zeph-4-1` prior to the "Modularize nice_view_gem" commit.

## What this added on top of stock M165437 nice-view-gem
- **Multi-theme animation system** (`widgets/animation.c` + `animation.h`):
  runtime `enum nice_view_theme { TRANSMUTATION, CRYSTAL }` with
  `nice_view_theme_set/get`, `nice_view_bind_screen`, `nice_view_theme_redraw`,
  `nice_view_animation_is_enabled`. Stock M165437 has only a single crystal
  animation with no theme switching.
- **`widgets/animation_assets.c/h`** — per-theme frame tables
  (`nice_view_anim_sets[]` / `nice_view_anim_lengths[]`).
- **`assets/animations/transmutation.c`** — the custom transmutation artwork.
- **`assets/animations/crystal.c`, `assets/crystal.c`, `assets/images.c`** —
  customized image assets.

## Integration points to re-apply on rebuild (were in the vendored shield)
- `widgets/screen_peripheral.c`: called `nice_view_bind_screen(widget->obj)` and
  `nice_view_theme_redraw()` in `zmk_widget_screen_init`.
- `Kconfig.defconfig`: `NICE_VIEW_ANIMATION_THEME_TRANSMUTATION` /
  `_THEME_CRYSTAL` bool options, plus `NICE_VIEW_ANIMATION` / `_MS` (note: stock
  M165437 uses `NICE_VIEW_GEM_ANIMATION` / `_MS` instead).
- `CMakeLists.txt`: compiled `animation_assets.c` + `assets/animations/*.c` on the
  peripheral path.
- The `cycle_animation` behavior was intended to drive `nice_view_theme_set` /
  toggle at runtime over the split (BEHAVIOR_LOCALITY_GLOBAL).
