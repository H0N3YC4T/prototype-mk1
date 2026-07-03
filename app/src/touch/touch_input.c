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
#include <zephyr/sys/atomic.h>
#include <zmk/behavior.h>

LOG_MODULE_REGISTER(mk1_touch, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_ZMK_POINTING)
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/pointing.h>

/* Trackpad tuning (WO-11) -- HW-calibrate on first pass. */
#define TP_SENS_NUM 1        /* pointer delta = screen delta * NUM / DEN */
#define TP_SENS_DEN 1
#define TP_HOLD_MS 400       /* tap-and-hold >= this = right click */
#define TP_DOUBLE_TAP_MS 350 /* two taps within this = left click */
#define TP_CORNER_PX 40      /* top-left NxN screen corner = exit tap */
#endif

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

/* Implemented by the prospector fork when the on-screen touch UI is present:
 * given the RAW rendered-screen coords (sx,sy; 280x240) it maps them to a cell
 * per whichever screen's grid is showing (2x3, or 4x3 for the numpad), then
 * navigates / fires and returns true if it consumed the tap. Weak default (no
 * fork UI) returns false, so we fall back to the fixed 2x3 macro grid below. */
__weak bool prospector_touch_tap(int sx, int sy) {
    ARG_UNUSED(sx);
    ARG_UNUSED(sy);
    return false;
}

/* Implemented by the fork while its trackpad page is showing: when true, touch_cb
 * streams pointer motion + clicks instead of dispatching cell taps. Weak default
 * false = trackpad feature absent (fork not present / not on that page). */
__weak bool prospector_touchpad_active(void) {
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

    /* Give the on-screen touch UI (prospector fork) first refusal: it maps the
     * raw screen coords to a cell per whichever screen (grid) is showing. */
    if (prospector_touch_tap(pending_sx, pending_sy)) {
        return;
    }

    /* Fallback (no fork UI): fixed 2x3 grid; the test macros live in the top row
     * (cells 0..2 = Vol- / Mute / Vol+); the other cells do nothing yet. */
    int cell = touch_cell(pending_sx, pending_sy);
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

#if IS_ENABLED(CONFIG_ZMK_POINTING)
/* Accumulated pointer deltas + a pending click, drained on the system workqueue.
 * ALL zmk_hid_/zmk_endpoint_ calls happen HERE, never in the input callback -- that
 * runs in driver context, and off-thread HID writes corrupt the shared report (the
 * boot-variant key corruption we already fixed once; same rule applies to mouse HID). */
static atomic_t tp_dx = ATOMIC_INIT(0);
static atomic_t tp_dy = ATOMIC_INIT(0);
static atomic_t tp_click = ATOMIC_INIT(0); /* 0 / MB1 / MB2 */
static int32_t tp_prev_sx, tp_prev_sy;
static bool tp_have_prev;
static bool tp_hold_fired;
static int64_t tp_last_tap_ms;

static void tp_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    int dx = atomic_set(&tp_dx, 0);
    int dy = atomic_set(&tp_dy, 0);
    int click = atomic_set(&tp_click, 0);
    if (dx || dy) {
        zmk_hid_mouse_movement_set((int16_t)dx, (int16_t)dy);
        zmk_endpoint_send_mouse_report();
        zmk_hid_mouse_movement_set(0, 0);
    }
    if (click) {
        zmk_hid_mouse_buttons_press((zmk_mouse_button_flags_t)click);
        zmk_endpoint_send_mouse_report();
        zmk_hid_mouse_buttons_release((zmk_mouse_button_flags_t)click);
        zmk_endpoint_send_mouse_report();
    }
}
static K_WORK_DEFINE(tp_work, tp_work_handler);

/* Fires TP_HOLD_MS after touch-down; cancelled on release or once travel exceeds the
 * tap threshold. If it fires, the finger is still down + roughly stationary = a
 * deliberate press-and-hold -> right click. */
static void tp_hold_handler(struct k_work *work) {
    ARG_UNUSED(work);
    tp_hold_fired = true;
    atomic_set(&tp_click, MB2);
    k_work_submit(&tp_work);
}
static K_WORK_DELAYABLE_DEFINE(tp_hold_work, tp_hold_handler);
#endif /* IS_ENABLED(CONFIG_ZMK_POINTING) */

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
        /* Edge-triggered: the CST816S re-reports BTN_TOUCH=1 every cycle while held,
         * so gate the down-logic on !active or it resets start_x/y (and tp_have_prev)
         * every report -> the trackpad never accumulates a motion delta. */
        if (evt->value && !active) {
            active = true;
            start_x = cur_x;
            start_y = cur_y;
#if IS_ENABLED(CONFIG_ZMK_POINTING)
            tp_have_prev = false;
            tp_hold_fired = false;
            if (prospector_touchpad_active()) {
                k_work_schedule(&tp_hold_work, K_MSEC(TP_HOLD_MS));
            }
#endif
        } else if (active) {
            active = false;
            bool is_tap = iabs32(cur_x - start_x) < TOUCH_TAP_MAX_TRAVEL &&
                          iabs32(cur_y - start_y) < TOUCH_TAP_MAX_TRAVEL;
#if IS_ENABLED(CONFIG_ZMK_POINTING)
            if (prospector_touchpad_active()) {
                k_work_cancel_delayable(&tp_hold_work);
                if (is_tap && !tp_hold_fired) {
                    int32_t ssx = panel_to_screen_x(start_x, start_y);
                    int32_t ssy = panel_to_screen_y(start_x, start_y);
                    if (ssx < TP_CORNER_PX && ssy < TP_CORNER_PX) {
                        /* corner tap = exit trackpad; hand to the fork UI */
                        pending_sx = ssx;
                        pending_sy = ssy;
                        k_work_submit(&touch_work);
                    } else {
                        int64_t now = k_uptime_get();
                        if (tp_last_tap_ms && (now - tp_last_tap_ms) < TP_DOUBLE_TAP_MS) {
                            atomic_set(&tp_click, MB1); /* double-tap = left click */
                            k_work_submit(&tp_work);
                            tp_last_tap_ms = 0;
                        } else {
                            tp_last_tap_ms = now; /* first tap; wait for a second */
                        }
                    }
                }
                break; /* trackpad mode owns the release; skip cell dispatch */
            }
#endif
            if (is_tap) {
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

#if IS_ENABLED(CONFIG_ZMK_POINTING)
    /* Trackpad motion: sample on every position event while touching -- NOT gated on
     * evt->sync. The CST816S carries sync on the BTN_TOUCH event, not the ABS events,
     * so a sync-gated sampler never fired during a drag. Incremental per-axis deltas
     * sum to the full move (works through the panel->screen rotation); the first
     * sample after a touch-down just seeds prev with no delta (no pointer jump). */
    if (prospector_touchpad_active() && active &&
        (evt->code == INPUT_ABS_X || evt->code == INPUT_ABS_Y)) {
        int32_t sx = panel_to_screen_x(cur_x, cur_y);
        int32_t sy = panel_to_screen_y(cur_x, cur_y);
        if (tp_have_prev) {
            int dx = (sx - tp_prev_sx) * TP_SENS_NUM / TP_SENS_DEN;
            int dy = (sy - tp_prev_sy) * TP_SENS_NUM / TP_SENS_DEN;
            if (dx || dy) {
                atomic_add(&tp_dx, dx);
                atomic_add(&tp_dy, dy);
                k_work_submit(&tp_work);
            }
        }
        tp_prev_sx = sx;
        tp_prev_sy = sy;
        tp_have_prev = true;
        if (iabs32(cur_x - start_x) >= TOUCH_TAP_MAX_TRAVEL ||
            iabs32(cur_y - start_y) >= TOUCH_TAP_MAX_TRAVEL) {
            k_work_cancel_delayable(&tp_hold_work); /* moved -> not a hold */
        }
    }
#endif
}
INPUT_CALLBACK_DEFINE(NULL, touch_cb, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s) */
