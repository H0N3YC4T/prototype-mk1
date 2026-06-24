/*
 * SPDX-License-Identifier: MIT
 *
 * Touch gesture + zone decoder for the XIAO nRF52840 + Waveshare 1.69"
 * prospector dongle (CST816S controller).
 *
 * Zephyr's input_cst816s driver reports raw touch points (INPUT_ABS_X / _ABS_Y
 * + INPUT_BTN_TOUCH press/release). This file collapses a touch-down -> drag ->
 * touch-up sequence into a gesture (tap, long-press, or vertical swipe) and, for
 * taps, the on-screen ZONE that was hit, then hands it to touch_dispatch().
 *
 * Branch dev/touch-testing: the dispatched actions DRIVE THE KEYBOARD. This
 * dongle is the split central, so it can inject ZMK behaviors / HID reports that
 * reach the host and the halves -- turning the touch panel into soft buttons:
 * layer toggles, BT-profile switching, media/volume, and an output toggle. The
 * gesture+zone pipeline below is complete (each event logs, so flash and watch
 * RTT/USB to confirm the controller + tune zones); the action bodies are
 * LOG_INF + TODO with the exact ZMK API named for each -- see TOUCH_TESTING.md.
 *
 * Zone model: the 240x280 panel is divided into a 2-column x 3-row grid of six
 * tap zones (no LVGL UI yet -- draw matching button art later in a prospector
 * widget; the coordinates are what matter for dispatch).
 */

#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mk1_touch, LOG_LEVEL_INF);

#if DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)

/* Panel geometry (Waveshare 1.69", portrait). Adjust if the driver reports a
 * rotated/oriented coordinate space. */
#define PANEL_W 240
#define PANEL_H 280

/* Travel (pixels) under which a touch counts as a tap; over which, a swipe. */
#define TOUCH_TAP_MAX_TRAVEL   24
#define TOUCH_SWIPE_MIN_TRAVEL 40
/* Hold time (ms) that turns a tap into a long-press. */
#define TOUCH_LONG_PRESS_MS    600

enum touch_gesture {
    TOUCH_GESTURE_NONE,
    TOUCH_GESTURE_TAP,
    TOUCH_GESTURE_LONG_PRESS,
    TOUCH_GESTURE_SWIPE_UP,
    TOUCH_GESTURE_SWIPE_DOWN,
};

static int32_t touch_start_x, touch_start_y;
static int32_t touch_cur_x, touch_cur_y;
static uint32_t touch_start_ms;
static bool touch_active;

static int32_t iabs(int32_t v) {
    return v < 0 ? -v : v;
}

/* 2 cols x 3 rows -> zones 0..5 (row-major). */
static int touch_zone(int32_t x, int32_t y) {
    int col = (x * 2) / PANEL_W;
    int row = (y * 3) / PANEL_H;
    if (col < 0) col = 0; if (col > 1) col = 1;
    if (row < 0) row = 0; if (row > 2) row = 2;
    return row * 2 + col;
}

static enum touch_gesture touch_classify(int32_t dx, int32_t dy, uint32_t held_ms) {
    if (iabs(dx) < TOUCH_TAP_MAX_TRAVEL && iabs(dy) < TOUCH_TAP_MAX_TRAVEL) {
        return held_ms >= TOUCH_LONG_PRESS_MS ? TOUCH_GESTURE_LONG_PRESS
                                              : TOUCH_GESTURE_TAP;
    }
    if (iabs(dy) > iabs(dx) && iabs(dy) >= TOUCH_SWIPE_MIN_TRAVEL) {
        return dy > 0 ? TOUCH_GESTURE_SWIPE_DOWN : TOUCH_GESTURE_SWIPE_UP;
    }
    return TOUCH_GESTURE_NONE; /* horizontal swipes unused on this branch */
}

static void touch_dispatch(enum touch_gesture gesture, int32_t x, int32_t y) {
    switch (gesture) {
    case TOUCH_GESTURE_SWIPE_UP:
        /* TODO: HID consumer Volume Up (zmk_hid_consumer_* + endpoints send). */
        LOG_INF("swipe UP   -> volume +            (TODO: HID consumer)");
        return;
    case TOUCH_GESTURE_SWIPE_DOWN:
        /* TODO: HID consumer Volume Down. */
        LOG_INF("swipe DOWN -> volume -            (TODO: HID consumer)");
        return;
    case TOUCH_GESTURE_TAP:
    case TOUCH_GESTURE_LONG_PRESS:
        break;
    default:
        return;
    }

    int zone = touch_zone(x, y);
    bool held = (gesture == TOUCH_GESTURE_LONG_PRESS);

    switch (zone) {
    case 0: /* TODO: zmk_keymap_layer_toggle(LOWER) */
        LOG_INF("tap zone 0 -> layer LOWER %-6s (TODO: zmk_keymap_layer_toggle)",
                held ? "(lock)" : "");
        break;
    case 1: /* TODO: zmk_keymap_layer_toggle(RAISE) */
        LOG_INF("tap zone 1 -> layer RAISE          (TODO: zmk_keymap_layer_toggle)");
        break;
    case 2: /* TODO: zmk_ble_prof_prev() / long-press zmk_ble_clear_bonds() */
        LOG_INF("tap zone 2 -> BT profile %-9s(TODO: zmk_ble_prof_*)",
                held ? "CLEAR" : "prev");
        break;
    case 3: /* TODO: zmk_ble_prof_next() */
        LOG_INF("tap zone 3 -> BT profile next      (TODO: zmk_ble_prof_next)");
        break;
    case 4: /* TODO: HID consumer Play/Pause */
        LOG_INF("tap zone 4 -> media play/pause     (TODO: HID consumer)");
        break;
    case 5: /* TODO: zmk_endpoints_toggle_transport() */
        LOG_INF("tap zone 5 -> output USB<->BLE     (TODO: zmk_endpoints_toggle)");
        break;
    default:
        break;
    }
}

static void touch_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    switch (evt->code) {
    case INPUT_ABS_X:
        touch_cur_x = evt->value;
        break;
    case INPUT_ABS_Y:
        touch_cur_y = evt->value;
        break;
    case INPUT_BTN_TOUCH:
        if (evt->value) {
            touch_active = true;
            touch_start_x = touch_cur_x;
            touch_start_y = touch_cur_y;
            touch_start_ms = k_uptime_get_32();
        } else if (touch_active) {
            touch_active = false;
            touch_dispatch(touch_classify(touch_cur_x - touch_start_x,
                                          touch_cur_y - touch_start_y,
                                          k_uptime_get_32() - touch_start_ms),
                           touch_cur_x, touch_cur_y);
        }
        break;
    default:
        break;
    }
}

INPUT_CALLBACK_DEFINE(NULL, touch_cb, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s) */
