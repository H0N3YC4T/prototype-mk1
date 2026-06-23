// widgets/animation.c
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <lvgl.h>
#include <zmk/display.h>

#include <zmk/event_manager.h>
#include <zmk/events/cycle_animation_state_changed.h>
#include "animation.h"
#include "animation_assets.h"

// Current theme and animation state
#if IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION_THEME_LANDSCAPE)
static enum nice_view_theme current_theme = NICE_VIEW_THEME_LANDSCAPE;
#elif IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION_THEME_EVANGELION)
static enum nice_view_theme current_theme = NICE_VIEW_THEME_EVANGELION;
#else
static enum nice_view_theme current_theme = NICE_VIEW_THEME_CRYSTAL;
#endif

// Animation movement state
#if IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION)
static bool nice_view_animation = true;
#else
static bool nice_view_animation = false;
#endif

// Horizontal offset for centering the animation
static lv_coord_t nice_view_theme_offset = 1;

// The LVGL parent/screen we draw into (bound from outside)
static lv_obj_t *nice_view_screen = NULL;

// The current LVGL object that holds the art (animimg or img)
static lv_obj_t *art_obj = NULL;

// Forward decl
static void calc_offset_for_theme(enum nice_view_theme theme);

/* -------------------------------------------------------------------------- */
/* Public helpers                                                             */
/* -------------------------------------------------------------------------- */

bool nice_view_animation_is_enabled(void) {
    return nice_view_animation;
}

void nice_view_bind_screen(lv_obj_t *screen) {
    nice_view_screen = screen;
}

/* Internal helper: redraw on the bound screen, if any */
void nice_view_theme_redraw(void) {
    if (!nice_view_screen) {
        return;
    }
    calc_offset_for_theme(nice_view_theme_get());
    draw_animation(nice_view_screen);
}

void nice_view_theme_set(enum nice_view_theme theme) {
    if (theme >= NICE_VIEW_THEME_COUNT) {
        theme = NICE_VIEW_THEME_CRYSTAL;
    }
    current_theme = theme;
}

enum nice_view_theme nice_view_theme_get(void) {
    return current_theme;
}



/* -------------------------------------------------------------------------- */
/* Offset calculation                                                         */
/* -------------------------------------------------------------------------- */
static void calc_offset_for_theme(enum nice_view_theme theme) {
    const lv_coord_t max_width = 120;
    const lv_img_dsc_t * const *frames = nice_view_anim_sets[theme];

    if (!frames || !frames[0]) {
        nice_view_theme_offset = 1;
        return;
    }

    lv_coord_t img_w = frames[0]->header.w;

    if (img_w >= max_width) {
        nice_view_theme_offset = 1;
    } else {
        nice_view_theme_offset = (max_width - img_w + 1) / 2;
    }
}

/* -------------------------------------------------------------------------- */
/* Main draw implementation                                                   */
/* -------------------------------------------------------------------------- */

void draw_animation(lv_obj_t *canvas) {
    if (!canvas) {
        return;
    }

    enum nice_view_theme theme = nice_view_theme_get();
    const lv_img_dsc_t * const *frames = nice_view_anim_sets[theme];
    const size_t frame_count = nice_view_anim_lengths[theme];

    if (!frames || frame_count == 0) {
        return; /* nothing to draw */
    }

    /* Delete previous art object, if any, so we don't accumulate LVGL objects. */
    if (art_obj) {
        lv_obj_del(art_obj);
        art_obj = NULL;
    }

    if (nice_view_animation) {
        art_obj = lv_animimg_create(canvas);
        lv_obj_center(art_obj);

        lv_animimg_set_src(art_obj, (const void **)frames, frame_count);
        lv_animimg_set_duration(art_obj, CONFIG_NICE_VIEW_ANIMATION_MS);
        lv_animimg_set_repeat_count(art_obj, LV_ANIM_REPEAT_INFINITE);
        lv_animimg_start(art_obj);
    } else {
        art_obj = lv_img_create(canvas);
        uint32_t idx = k_uptime_get_32() % frame_count;
        lv_img_set_src(art_obj, frames[idx]);
    }

    lv_obj_align(art_obj, LV_ALIGN_TOP_LEFT, nice_view_theme_offset, 0);
}

/* -------------------------------------------------------------------------- */
/* Hotkey: cycle_animation behavior event                                     */
/* -------------------------------------------------------------------------- */

static int nice_view_cycle_animation_listener(const zmk_event_t *eh) {
    const struct cycle_animation_state_changed *ev = as_cycle_animation_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->type) {
    case NVC_NEXT:
        nice_view_theme_set((nice_view_theme_get() + 1) % NICE_VIEW_THEME_COUNT);
        nice_view_theme_redraw();
        break;
    case NVC_TOGGLE:
        nice_view_animation = !nice_view_animation;
        nice_view_theme_redraw();
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(nice_view_anim_hotkey, nice_view_cycle_animation_listener);
ZMK_SUBSCRIPTION(nice_view_anim_hotkey, cycle_animation_state_changed);


