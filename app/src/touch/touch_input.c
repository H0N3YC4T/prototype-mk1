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

/* The OPERATOR screen renders rotated 90 deg CLOCKWISE vs the CST816S panel axes
 * (confirmed on hardware: panel-X runs along the screen's vertical). Map raw
 * panel (tx,ty) -> rendered screen (sx,sy); the screen is 280 wide x 240 tall.
 * If the 3 keys come out left<->right mirrored, flip sx to (PANEL_H - ty). */
#define SCREEN_W 280
#define SCREEN_H 240
static inline int32_t panel_to_screen_x(int32_t tx, int32_t ty) { return ty; }
static inline int32_t panel_to_screen_y(int32_t tx, int32_t ty) { return PANEL_W - tx; }

/* Touch is a fixed 2-row x 3-column grid over the WHOLE screen, edge to edge (no
 * gaps -> no dead zones / mis-inputs). Cells are row-major 0..5:
 *     0 1 2   (top row)
 *     3 4 5   (bottom row)
 * Each macro button occupies one cell. The current test macros use the top row. */
#define GRID_COLS 3
#define GRID_ROWS 2

/* One ZMK macro behavior per zone -- defined in prototype_mk1_waveshare.overlay.
 * Customise what they type there; this file only dispatches by zone. */
static const char *const touch_macro_dev[3] = {
    "touch_macro_0",
    "touch_macro_1",
    "touch_macro_2",
};

static int32_t cur_x, cur_y, start_x, start_y;
static bool active;
static int32_t pending_sx, pending_sy;

/* Implemented by the prospector fork when the on-screen touch UI (the two-screen
 * macro pad) is present: given the tapped grid cell (0..5) it opens the macro
 * screen, hits Back, or fires the button in that cell, and returns true if it
 * consumed the tap. Weak default (no fork UI yet) returns false, so we fall back
 * to firing a macro directly by cell -- the current behaviour. */
__weak bool prospector_touch_tap(int cell) {
    ARG_UNUSED(cell);
    return false;
}

static inline int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

/* Map a screen coordinate to one of the 6 grid cells (0..5), edge to edge. */
static int touch_cell(int32_t sx, int32_t sy) {
    if (sx < 0) sx = 0;
    if (sx >= SCREEN_W) sx = SCREEN_W - 1;
    if (sy < 0) sy = 0;
    if (sy >= SCREEN_H) sy = SCREEN_H - 1;
    int col = (sx * GRID_COLS) / SCREEN_W;
    int row = (sy * GRID_ROWS) / SCREEN_H;
    return row * GRID_COLS + col;
}

/* Run the ZMK behavior off the input callback context (which may be IRQ/driver
 * context) -- behaviors/HID must run on a thread. */
static void touch_fire(struct k_work *work) {
    ARG_UNUSED(work);
    int cell = touch_cell(pending_sx, pending_sy);

    /* Give the on-screen touch UI (prospector fork) first refusal: on the main
     * screen it opens the macro screen, on the macro screen it handles the macro
     * buttons / Back / timeout itself. */
    if (prospector_touch_tap(cell)) {
        return;
    }

    /* Fallback (no fork UI): the current test macros live in the top row
     * (cells 0..2 = Vol- / Mute / Vol+); the other cells do nothing yet. */
    if (cell < 0 || cell > 2) {
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = touch_macro_dev[cell],
        .param1 = 0,
        .param2 = 0,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = (uint32_t)(0x7000 + cell), /* synthetic, off-matrix */
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
                pending_sx = panel_to_screen_x(cur_x, cur_y);
                pending_sy = panel_to_screen_y(cur_x, cur_y);
                LOG_INF("tap raw(%d,%d) screen(%d,%d) -> cell %d",
                        cur_x, cur_y, pending_sx, pending_sy,
                        touch_cell(pending_sx, pending_sy));
                /* Always dispatch -- the fork UI may want a tap anywhere (e.g.
                 * to open the macro screen from the main screen). */
                k_work_submit(&touch_work);
            }
        }
        break;
    default:
        break;
    }
}
INPUT_CALLBACK_DEFINE(NULL, touch_cb, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s) */
