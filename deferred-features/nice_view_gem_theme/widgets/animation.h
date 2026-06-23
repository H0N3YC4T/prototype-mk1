// widgets/animation.h
#pragma once

#include <lvgl.h>

// Theme selection
enum nice_view_theme {
    NICE_VIEW_THEME_TRANSMUTATION = 0,
    NICE_VIEW_THEME_CRYSTAL,
    NICE_VIEW_THEME_COUNT,
};


/**
 * @brief Return whether animation mode is enabled (true) or static-image mode (false).
 */
bool nice_view_animation_is_enabled(void);

/**
 * @brief One-time init hook (optional; currently does not draw).
 *
 * Call this during your display/widget init if you want a formal init step.
 */
void nice_view_theme_redraw(void);

/**
 * @brief Bind the LVGL screen/container that the animation should draw into.
 *
 * Usually this is the same object you create in zmk_widget_screen_init()
 * (e.g. widget->obj).
 */
void nice_view_bind_screen(lv_obj_t *screen);

/**
 * @brief Draw the current theme onto the given canvas object.
 *
 * This does NOT remember the canvas; it just draws into the one you pass.
 * Theme and animation state are taken from the nice_view_* API.
 */
void draw_animation(lv_obj_t *canvas);

/**
 * @brief Set the current theme and redraw (if a screen is bound).
 */
void nice_view_theme_set(enum nice_view_theme theme);

/**
 * @brief Get the current theme.
 */
enum nice_view_theme nice_view_theme_get(void);

