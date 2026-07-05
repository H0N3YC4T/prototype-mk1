# CMakeLists.txt at repo root (module)
zephyr_include_directories(include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Custom cycle_animation behavior + its event (drives the nice!view theme hotkey).
# behavior_cycle_animation.c is guarded internally by DT_HAS_COMPAT_STATUS_OKAY,
# so it only generates an instance when the keymap declares a
# zmk,behavior-cycle-animation node.
target_sources(app PRIVATE app/src/events/cycle_animation_state_changed.c)
target_sources(app PRIVATE app/src/behaviors/behavior_cycle_animation.c)

# CST816S touch input for the XIAO + Waveshare prospector dongle: tap dispatch to
# the fork's on-screen UI + trackpad gestures (see the file header). Internally
# gated on DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s), so it compiles to nothing
# until the CST816S node is declared (see docs/information.md).
target_sources(app PRIVATE app/src/touch/touch_input.c)