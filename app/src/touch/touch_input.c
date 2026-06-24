/*
 * SPDX-License-Identifier: MIT
 *
 * Touch gesture decoder for the XIAO nRF52840 + Waveshare 1.69" prospector
 * dongle (CST816S controller).
 *
 * Zephyr's input_cst816s driver reports raw touch points (INPUT_ABS_X / _ABS_Y
 * + INPUT_BTN_TOUCH press/release). This file collapses a touch-down -> drag ->
 * touch-up sequence into a single gesture (tap or four-way swipe) and hands it
 * to touch_dispatch().
 *
 * Branch dev/touch-easy: the dispatched actions are display-LOCAL -- they only
 * affect this dongle's own screen (page the prospector status screens, change
 * display brightness, pause/advance the artwork theme). The gesture pipeline
 * below is complete and meant to be flashed first to confirm the controller is
 * wired correctly: every gesture logs a line, so you can watch over RTT/USB and
 * tune the thresholds before wiring the real actions. The action bodies are
 * LOG_INF + TODO until the prospector screen/brightness hooks are connected --
 * see TOUCH_EASY.md.
 */

#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mk1_touch, LOG_LEVEL_INF);

#if DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)

/* Travel (pixels) under which a touch counts as a tap; over which, a swipe. */
#define TOUCH_TAP_MAX_TRAVEL   24
#define TOUCH_SWIPE_MIN_TRAVEL 40

enum touch_gesture {
    TOUCH_GESTURE_NONE,
    TOUCH_GESTURE_TAP,
    TOUCH_GESTURE_SWIPE_LEFT,
    TOUCH_GESTURE_SWIPE_RIGHT,
    TOUCH_GESTURE_SWIPE_UP,
    TOUCH_GESTURE_SWIPE_DOWN,
};

static int32_t touch_start_x, touch_start_y;
static int32_t touch_cur_x, touch_cur_y;
static bool touch_active;

static int32_t iabs(int32_t v) {
    return v < 0 ? -v : v;
}

static enum touch_gesture touch_classify(int32_t dx, int32_t dy) {
    if (iabs(dx) < TOUCH_TAP_MAX_TRAVEL && iabs(dy) < TOUCH_TAP_MAX_TRAVEL) {
        return TOUCH_GESTURE_TAP;
    }
    if (iabs(dx) >= iabs(dy)) {
        if (iabs(dx) < TOUCH_SWIPE_MIN_TRAVEL) {
            return TOUCH_GESTURE_NONE;
        }
        return dx > 0 ? TOUCH_GESTURE_SWIPE_RIGHT : TOUCH_GESTURE_SWIPE_LEFT;
    }
    if (iabs(dy) < TOUCH_SWIPE_MIN_TRAVEL) {
        return TOUCH_GESTURE_NONE;
    }
    return dy > 0 ? TOUCH_GESTURE_SWIPE_DOWN : TOUCH_GESTURE_SWIPE_UP;
}

static void touch_dispatch(enum touch_gesture gesture) {
    switch (gesture) {
    case TOUCH_GESTURE_SWIPE_RIGHT:
        /* TODO(easy): advance to the next prospector status screen. */
        LOG_INF("swipe RIGHT -> next status screen   (TODO: prospector hook)");
        break;
    case TOUCH_GESTURE_SWIPE_LEFT:
        /* TODO(easy): go to the previous prospector status screen. */
        LOG_INF("swipe LEFT  -> prev status screen   (TODO: prospector hook)");
        break;
    case TOUCH_GESTURE_SWIPE_UP:
        /* TODO(easy): raise display brightness (prospector brightness API). */
        LOG_INF("swipe UP    -> brightness +         (TODO: prospector brightness)");
        break;
    case TOUCH_GESTURE_SWIPE_DOWN:
        /* TODO(easy): lower display brightness. */
        LOG_INF("swipe DOWN  -> brightness -         (TODO: prospector brightness)");
        break;
    case TOUCH_GESTURE_TAP:
        /* TODO(easy): pause / advance the artwork theme. This dongle is the
         * split central, so it must relay cycle_animation to the peripheral
         * gems rather than handle it locally (see TOUCH_EASY.md). */
        LOG_INF("tap         -> theme pause/next     (TODO: cycle_animation relay)");
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
        } else if (touch_active) {
            touch_active = false;
            touch_dispatch(touch_classify(touch_cur_x - touch_start_x,
                                          touch_cur_y - touch_start_y));
        }
        break;
    default:
        break;
    }
}

INPUT_CALLBACK_DEFINE(NULL, touch_cb, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s) */
