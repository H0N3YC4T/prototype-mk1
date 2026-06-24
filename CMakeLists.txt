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

# Touch gesture decoder for the XIAO + Waveshare prospector dongle. Guarded
# internally by DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s), so it is inert on
# every build target except the one whose shield declares the CST816S node.
target_sources(app PRIVATE app/src/touch/touch_input.c)