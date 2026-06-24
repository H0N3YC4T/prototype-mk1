// widgets/animation_assets.h
#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#include "animation.h"

// One pointer-array per theme, indexed by enum nice_view_theme.
extern const lv_img_dsc_t * const *nice_view_anim_sets[NICE_VIEW_THEME_COUNT];
extern const size_t nice_view_anim_lengths[NICE_VIEW_THEME_COUNT];
// Per-theme per-frame dwell time in ms (animations fast, slideshows slow).
extern const uint32_t nice_view_anim_frame_ms[NICE_VIEW_THEME_COUNT];