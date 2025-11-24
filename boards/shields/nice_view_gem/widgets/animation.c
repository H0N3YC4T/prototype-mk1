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
#if IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION_THEME_TRANSMUTATION)
static enum nice_view_theme current_theme = NICE_VIEW_THEME_TRANSMUTATION;
#endif

#if IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION_THEME_OMNISSIAH)
static enum nice_view_theme current_theme = NICE_VIEW_THEME_OMNISSIAH;
#endif

#if IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION_THEME_ULTRAMAR)
static enum nice_view_theme current_theme = NICE_VIEW_THEME_ULTRAMAR;
#endif

#if IS_ENABLED(CONFIG_NICE_VIEW_ANIMATION_THEME_CRYSTAL)
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
        theme = NICE_VIEW_THEME_TRANSMUTATION;
    }
    current_theme = theme;
}

enum nice_view_theme nice_view_theme_get(void) {
    return current_theme;
}

void nice_view_theme_next(void) {
    enum nice_view_theme theme = nice_view_theme_get();
    theme = (theme + 1) % NICE_VIEW_THEME_COUNT;
    nice_view_theme_set(theme);
    nice_view_theme_redraw();
}

void nice_view_animation_toggle(void) {
    nice_view_animation = !nice_view_animation;
        if (!nice_view_screen) {
            draw_animation(nice_view_screen);
        }
}

void nice_view_animation_off(void) {
    if (nice_view_animation) {
        nice_view_animation = false;
        if (!nice_view_screen) {
            draw_animation(nice_view_screen);
        }
    }
}

void nice_view_animation_on(void) {
    if (!nice_view_animation) {
        nice_view_animation = true;
        if (!nice_view_screen) {
            draw_animation(nice_view_screen);
        }
    }
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
/* Event listener: respond to cycle_animation_state_changed                   */
/* -------------------------------------------------------------------------- */
static void handle_cycle_animation_type(int type) {
    switch (type) {
    case NVC_TOGGLE:
        nice_view_animation_toggle();
        break;

    case NVC_NEXT:
        nice_view_theme_next();
        break;

    default:
        /* Unknown type; ignore silently. */
        break;
    }
}

static int nice_view_cycle_animation_listener(const zmk_event_t *eh)
{
    const struct cycle_animation_state_changed *evt =
        as_cycle_animation_state_changed(eh);
    if (!evt) {
        return 0;
    }

    handle_cycle_animation_type(evt->type);
    return 0;
}

// ZMK_LISTENER(nice_view_cycle_animation_listener, nice_view_cycle_animation_listener);
// ZMK_SUBSCRIPTION(nice_view_cycle_animation_listener, cycle_animation_state_changed);
