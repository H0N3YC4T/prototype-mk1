/*
 * SPDX-License-Identifier: MIT
 *
 * Touch -> 3 macro keys for the XIAO nRF52840 + Waveshare 1.69" prospector
 * dongle (CST816S controller). The 3 zones replace the on-screen wpm_meter
 * widget (WPM bars + WPM number + layer-name text); each tap fires one of three
 * ZMK macro behaviors (touch_macro_0/1/2), which the dongle -- being the split
 * central -- sends to the host as a normal HID report.
 *
 * Whole file is gated on the cst816s DT node, so with no node present (the
 * default until the controller is wired + declared in the overlay) this
 * compiles to nothing and the build stays green. See TOUCH-SCREEN-NOTES.md for
 * the activation steps (wiring/pins, the DT node, the 3 macro defs, and the
 * zone calibration).
 */

#include <zephyr/kernel.h>

#if DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s)

#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

LOG_MODULE_REGISTER(mk1_touch, LOG_LEVEL_INF);

/* Waveshare 1.69" panel, as the CST816S reports it (portrait). The rendered
 * OPERATOR screen is rotated relative to this, so the zone bounds below are a
 * starting guess -- every tap logs raw x,y + the computed zone so the constants
 * can be calibrated on hardware (see notes). */
#define PANEL_W 240
#define PANEL_H 280
#define TOUCH_TAP_MAX_TRAVEL 24

/* Y band (panel coords) approximating the wpm_meter strip; taps outside it are
 * ignored so the rest of the screen (battery/output) isn't touch-active. */
#define ZONE_Y_MIN 40
#define ZONE_Y_MAX 140

/* One ZMK macro behavior per zone -- defined in prototype_mk1_waveshare.overlay.
 * Customise what they type there; this file only dispatches by zone. */
static const char *const touch_macro_dev[3] = {
    "touch_macro_0",
    "touch_macro_1",
    "touch_macro_2",
};

static int32_t cur_x, cur_y, start_x, start_y;
static bool active;
static int pending_zone = -1;

static inline int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

/* Returns 0..2 for a hit, or -1 outside the macro-key strip. */
static int touch_zone(int32_t x, int32_t y) {
    if (y < ZONE_Y_MIN || y > ZONE_Y_MAX) {
        return -1;
    }
    int z = (x * 3) / PANEL_W;
    if (z < 0) z = 0;
    if (z > 2) z = 2;
    return z;
}

/* Run the ZMK behavior off the input callback context (which may be IRQ/driver
 * context) -- behaviors/HID must run on a thread. */
static void touch_fire(struct k_work *work) {
    ARG_UNUSED(work);
    int zone = pending_zone;
    if (zone < 0 || zone > 2) {
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = touch_macro_dev[zone],
        .param1 = 0,
        .param2 = 0,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = (uint32_t)(0x7000 + zone), /* synthetic, off-matrix */
        .timestamp = k_uptime_get(),
    };

    zmk_behavior_invoke_binding(&binding, event, true);
    zmk_behavior_invoke_binding(&binding, event, false);
}
static K_WORK_DEFINE(touch_work, touch_fire);

static void touch_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    switch (evt->code) {
    case INPUT_ABS_X:
        cur_x = evt->value;
        break;
    case INPUT_ABS_Y:
        cur_y = evt->value;
        break;
    case INPUT_BTN_TOUCH:
        if (evt->value) {
            active = true;
            start_x = cur_x;
            start_y = cur_y;
        } else if (active) {
            active = false;
            if (iabs32(cur_x - start_x) < TOUCH_TAP_MAX_TRAVEL &&
                iabs32(cur_y - start_y) < TOUCH_TAP_MAX_TRAVEL) {
                int zone = touch_zone(cur_x, cur_y);
                LOG_INF("tap x=%d y=%d -> zone %d", cur_x, cur_y, zone);
                if (zone >= 0) {
                    pending_zone = zone;
                    k_work_submit(&touch_work);
                }
            }
        }
        break;
    default:
        break;
    }
}
INPUT_CALLBACK_DEFINE(NULL, touch_cb, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s) */
