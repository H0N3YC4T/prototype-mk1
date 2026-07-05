/*
 * SPDX-License-Identifier: MIT
 *
 * CST816S touch input for the XIAO nRF52840 + Waveshare 1.69" prospector
 * dongle. Two jobs:
 *
 *  1. TAPS: raw screen coords are handed to the prospector fork's on-screen UI
 *     (prospector_touch_tap), which maps them to a cell in whichever screen is
 *     showing. With no fork UI present, falls back to a fixed 2x3 macro grid
 *     (touch_macro_0..2, defined in prototype_mk1_waveshare.overlay).
 *
 *  2. TRACKPAD: while the fork's MOUSE page is active
 *     (prospector_touchpad_active), gestures become mouse HID instead --
 *     drag = pointer, right-edge drag = scroll, 1 tap = left click,
 *     2 taps = right click, top-left corner tap = exit.
 *
 * Whole file is gated on the cst816s DT node, so with no node present this
 * compiles to nothing and the build stays green. Thread rule: behaviors and
 * HID writes happen ONLY on the system workqueue (k_work), never in this
 * file's input callback (driver context). See TOUCH-SCREEN-NOTES.md.
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

/* Whole-screen trackpad (single-touch CST816S -- no two-finger gestures):
 *   DRAG (main area)     -> pointer motion (commits once past the dead-zone).
 *   DRAG on the far-right -> scroll (that vertical strip is the scroll lane).
 *   1 TAP -> left click,  2 TAPS -> right click (resolved over TP_DTAP_MS).
 *   top-left corner TAP -> exit to the hub. */
#define TP_CORNER_PX 40           /* top-left NxN screen corner tap = exit */
#define TP_MOVE_DEADZONE_PX 8     /* travel this far commits a drag (else it's a tap) */
#define TP_DTAP_MS 180            /* 2nd tap within this of the 1st = right click */
#define TP_SCROLL_ZONE_X 240      /* screen x >= this (far right, of 280) = scroll lane */
#define TP_SCROLL_PX 18           /* screen px of vertical drag per wheel tick */

/* Pointer acceleration: threshold curve in x256 fixed point. Per-sample deltas up
 * to TP_ACCEL_THRESH px move 1:1 (precision), above it the gain ramps linearly
 * with speed and saturates at the level's cap. Level 0 = flat / off. The level is
 * adjustable from the fork's settings screen (prospector_touchpad_accel_*). */
#define TP_ACCEL_THRESH 3
static const uint16_t tp_accel_coef[] = {0, 10, 18, 30, 46};   /* gain256 per px over */
static const uint16_t tp_accel_cap[] = {256, 448, 576, 736, 896}; /* 1.0x .. 3.5x */
#define TP_ACCEL_LEVELS ((int)ARRAY_SIZE(tp_accel_coef))
#define TP_ACCEL_DEFAULT 2
#endif

/* Waveshare 1.69" panel, as the CST816S reports it (portrait). */
#define PANEL_W 240
#define PANEL_H 280
#define TOUCH_TAP_MAX_TRAVEL 24

/* The OPERATOR screen renders rotated 90 deg CLOCKWISE vs the CST816S panel axes
 * (panel-X runs along the screen's vertical). Map raw panel (tx,ty) -> rendered
 * screen (sx,sy); the screen is 280 wide x 240 tall. If taps ever come out
 * left<->right mirrored, flip sx to (PANEL_H - ty). */
#define SCREEN_W 280
#define SCREEN_H 240
static inline int32_t panel_to_screen_x(int32_t tx, int32_t ty) { return ty; }
static inline int32_t panel_to_screen_y(int32_t tx, int32_t ty) { return PANEL_W - tx; }

/* Touch is a fixed 2-row x 3-column grid over the WHOLE screen, edge to edge (no
 * gaps -> no dead zones / mis-inputs). Cells are row-major 0..5:
 *     0 1 2   (top row)
 *     3 4 5   (bottom row)
 * Each macro button occupies one cell; the fallback macros use the top row. */
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
/* Pending click + accumulated scroll/motion, drained on the system workqueue. ALL
 * zmk_hid_/zmk_endpoint_ calls happen HERE, never in the input callback -- that runs
 * in driver context, and ZMK's HID reports have no cross-thread locking. */
enum tp_mode { TP_PENDING, TP_MOTION, TP_SCROLL };
static atomic_t tp_click = ATOMIC_INIT(0);  /* 0 / MB1 / MB2 */
static atomic_t tp_scroll = ATOMIC_INIT(0); /* signed vertical wheel ticks pending */
static atomic_t tp_dx = ATOMIC_INIT(0);     /* accumulated pointer deltas (raw px) */
static atomic_t tp_dy = ATOMIC_INIT(0);
static int32_t tp_start_sx, tp_start_sy;    /* touch-down screen coords */
static int32_t tp_prev_sx, tp_prev_sy;      /* last sampled point while streaming motion */
static int32_t tp_scroll_ref_sy;            /* screen-y of the last emitted scroll tick */
static enum tp_mode tp_mode;                /* this touch: pending / moving / scrolling */
static bool tp_scroll_zone;                 /* touch started in the far-right scroll lane */
static bool tp_first_tap;                   /* a first tap is awaiting a possible second */
static int32_t tp_carry_x, tp_carry_y;      /* x256 sub-pixel remainders (accel scaling) */
static uint8_t tp_accel_level = TP_ACCEL_DEFAULT;

/* Settings-screen hooks (called from the fork's display thread; a single aligned
 * byte read/write, so no marshalling needed). */
int prospector_touchpad_accel_get(void) {
    return tp_accel_level;
}
void prospector_touchpad_accel_step(int delta) {
    int l = (int)tp_accel_level + delta;
    if (l < 0) { l = 0; }
    if (l > TP_ACCEL_LEVELS - 1) { l = TP_ACCEL_LEVELS - 1; }
    tp_accel_level = (uint8_t)l;
}

/* Gain (x256) for one sample whose |dx|+|dy| is `mag`. */
static int32_t tp_accel_gain256(int32_t mag) {
    int32_t over = mag - TP_ACCEL_THRESH;
    if (over <= 0) {
        return 256;
    }
    int32_t g = 256 + (int32_t)tp_accel_coef[tp_accel_level] * over;
    int32_t cap = tp_accel_cap[tp_accel_level];
    return g > cap ? cap : g;
}

static void tp_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    int dx = atomic_set(&tp_dx, 0); /* already accel-scaled at accumulation time */
    int dy = atomic_set(&tp_dy, 0);
    int scroll = atomic_set(&tp_scroll, 0);
    int click = atomic_set(&tp_click, 0);
    if (dx || dy) {
        zmk_hid_mouse_movement_set((int16_t)dx, (int16_t)dy);
        zmk_endpoint_send_mouse_report();
        zmk_hid_mouse_movement_set(0, 0);
    }
    if (scroll) {
        zmk_hid_mouse_scroll_set(0, (int16_t)scroll);
        zmk_endpoint_send_mouse_report();
        zmk_hid_mouse_scroll_set(0, 0);
    }
    if (click) {
        zmk_hid_mouse_buttons_press((zmk_mouse_button_flags_t)click);
        zmk_endpoint_send_mouse_report();
        zmk_hid_mouse_buttons_release((zmk_mouse_button_flags_t)click);
        zmk_endpoint_send_mouse_report();
    }
}
static K_WORK_DEFINE(tp_work, tp_work_handler);

/* Fires TP_DTAP_MS after a first tap released with no second tap -> single tap = left
 * click. (A second tap inside the window fires the right click directly + cancels this.) */
static void tp_tap_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (tp_first_tap) {
        tp_first_tap = false;
        atomic_set(&tp_click, MB1);
        k_work_submit(&tp_work);
    }
}
static K_WORK_DELAYABLE_DEFINE(tp_tap_work, tp_tap_handler);
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
        /* Edge-triggered: the CST816S re-reports BTN_TOUCH=1 every cycle while held.
         * Gate down on !active so start_x/y (the zone anchor) is captured ONCE, and
         * gate up explicitly on !evt->value so a repeated held report is not mistaken
         * for a release. */
        if (evt->value && !active) {
            active = true;
            start_x = cur_x;
            start_y = cur_y;
#if IS_ENABLED(CONFIG_ZMK_POINTING)
            tp_start_sx = panel_to_screen_x(start_x, start_y);
            tp_start_sy = panel_to_screen_y(start_x, start_y);
            tp_mode = TP_PENDING;
            tp_scroll_zone = (tp_start_sx >= TP_SCROLL_ZONE_X);
            tp_carry_x = 0;
            tp_carry_y = 0;
#endif
        } else if (!evt->value && active) {
            active = false;
#if IS_ENABLED(CONFIG_ZMK_POINTING)
            if (prospector_touchpad_active()) {
                /* A "tap" = finger lifted before committing to a drag (scroll or move). */
                if (tp_mode == TP_PENDING) {
                    if (tp_start_sx < TP_CORNER_PX && tp_start_sy < TP_CORNER_PX) {
                        /* corner tap = exit; hand to the fork UI. Drop any pending
                         * first-tap so a stray left click doesn't fire after exit. */
                        tp_first_tap = false;
                        k_work_cancel_delayable(&tp_tap_work);
                        pending_sx = tp_start_sx;
                        pending_sy = tp_start_sy;
                        k_work_submit(&touch_work);
                    } else if (tp_first_tap) {
                        /* second tap within the window = right click */
                        tp_first_tap = false;
                        k_work_cancel_delayable(&tp_tap_work);
                        atomic_set(&tp_click, MB2);
                        k_work_submit(&tp_work);
                    } else {
                        /* first tap -> defer the left click, wait for a possible second */
                        tp_first_tap = true;
                        k_work_reschedule(&tp_tap_work, K_MSEC(TP_DTAP_MS));
                    }
                }
                break; /* trackpad mode owns the release; skip cell dispatch */
            }
#endif
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

#if IS_ENABLED(CONFIG_ZMK_POINTING)
    /* Drag handling, sampled on position events (not evt->sync -- the CST816S carries
     * sync on BTN_TOUCH, not the ABS events). */
    if (prospector_touchpad_active() && active &&
        (evt->code == INPUT_ABS_X || evt->code == INPUT_ABS_Y)) {
        int32_t sx = panel_to_screen_x(cur_x, cur_y);
        int32_t sy = panel_to_screen_y(cur_x, cur_y);
        if (tp_mode == TP_PENDING) {
            /* Leaving the dead-zone commits the drag: scroll lane -> scroll, else move. */
            if (iabs32(sx - tp_start_sx) >= TP_MOVE_DEADZONE_PX ||
                iabs32(sy - tp_start_sy) >= TP_MOVE_DEADZONE_PX) {
                if (tp_scroll_zone) {
                    tp_mode = TP_SCROLL;
                    tp_scroll_ref_sy = sy;
                } else {
                    tp_mode = TP_MOTION;
                    tp_prev_sx = sx; /* stream from here; the dead-zone px are dropped */
                    tp_prev_sy = sy;
                }
            }
        } else if (tp_mode == TP_MOTION) {
            int dx = sx - tp_prev_sx;
            int dy = sy - tp_prev_sy;
            tp_prev_sx = sx;
            tp_prev_sy = sy;
            if (dx || dy) {
                /* Accelerate per sample (per-sample delta ~ finger speed at a fixed
                 * report rate); carry the x256 remainders so slow motion keeps its
                 * sub-pixel travel instead of rounding to nothing. */
                int32_t gain = tp_accel_gain256(iabs32(dx) + iabs32(dy));
                tp_carry_x += dx * gain;
                tp_carry_y += dy * gain;
                int outx = tp_carry_x / 256;
                int outy = tp_carry_y / 256;
                tp_carry_x -= outx * 256;
                tp_carry_y -= outy * 256;
                if (outx || outy) {
                    atomic_add(&tp_dx, outx);
                    atomic_add(&tp_dy, outy);
                    k_work_submit(&tp_work);
                }
            }
        } else { /* TP_SCROLL: one wheel tick per TP_SCROLL_PX of vertical travel */
            int dsy = sy - tp_scroll_ref_sy;
            if (dsy >= TP_SCROLL_PX || dsy <= -TP_SCROLL_PX) {
                int ticks = dsy / TP_SCROLL_PX; /* signed */
                /* finger down the screen (sy up) -> scroll down (negative wheel). Flip
                 * this sign if it scrolls the wrong way for your taste. */
                atomic_add(&tp_scroll, -ticks);
                tp_scroll_ref_sy += ticks * TP_SCROLL_PX;
                k_work_submit(&tp_work);
            }
        }
    }
#endif
}
INPUT_CALLBACK_DEFINE(NULL, touch_cb, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(hynitron_cst816s) */
