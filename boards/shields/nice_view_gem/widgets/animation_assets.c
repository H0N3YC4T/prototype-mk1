// widgets/animation_assets.c

#include <lvgl.h>
#include "animation.h"
#include "animation_assets.h"

#define NICE_VIEW_ANIM_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// -------------------------------------------------------------------------------
// Crystal theme (crystal.c)
LV_IMG_DECLARE(crystal_01);
LV_IMG_DECLARE(crystal_02);
LV_IMG_DECLARE(crystal_03);
LV_IMG_DECLARE(crystal_04);
LV_IMG_DECLARE(crystal_05);
LV_IMG_DECLARE(crystal_06);
LV_IMG_DECLARE(crystal_07);
LV_IMG_DECLARE(crystal_08);
LV_IMG_DECLARE(crystal_09);
LV_IMG_DECLARE(crystal_10);
LV_IMG_DECLARE(crystal_11);
LV_IMG_DECLARE(crystal_12);
LV_IMG_DECLARE(crystal_13);
LV_IMG_DECLARE(crystal_14);
LV_IMG_DECLARE(crystal_15);
LV_IMG_DECLARE(crystal_16);

static const lv_img_dsc_t *crystal_imgs[] = {
    &crystal_01, &crystal_02, &crystal_03, &crystal_04,
    &crystal_05, &crystal_06, &crystal_07, &crystal_08,
    &crystal_09, &crystal_10, &crystal_11, &crystal_12,
    &crystal_13, &crystal_14, &crystal_15, &crystal_16,
};

// -------------------------------------------------------------------------------
// Landscape theme (landscape.c): dunes, dunes 2, pyramids, mountain, mountains 2
LV_IMG_DECLARE(landscape_01);
LV_IMG_DECLARE(landscape_03);
LV_IMG_DECLARE(landscape_06);
LV_IMG_DECLARE(landscape_07);
LV_IMG_DECLARE(landscape_08);

static const lv_img_dsc_t *landscape_imgs[] = {
    &landscape_01, &landscape_03, &landscape_06, &landscape_07, &landscape_08,
};

// -------------------------------------------------------------------------------
// Evangelion theme (evangelion.c): spaceships 1-3, evangelion 1-3
LV_IMG_DECLARE(evangelion_02);
LV_IMG_DECLARE(evangelion_04);
LV_IMG_DECLARE(evangelion_05);
LV_IMG_DECLARE(evangelion_09);
LV_IMG_DECLARE(evangelion_10);
LV_IMG_DECLARE(evangelion_11);

static const lv_img_dsc_t *evangelion_imgs[] = {
    &evangelion_02, &evangelion_04, &evangelion_05,
    &evangelion_09, &evangelion_10, &evangelion_11,
};

// -------------------------------------------------------------------------------
// Lookup Table
const lv_img_dsc_t * const *nice_view_anim_sets[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_CRYSTAL]    = crystal_imgs,
    [NICE_VIEW_THEME_LANDSCAPE]  = landscape_imgs,
    [NICE_VIEW_THEME_EVANGELION] = evangelion_imgs,
};

// Frame Counter
const size_t nice_view_anim_lengths[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_CRYSTAL]    = NICE_VIEW_ANIM_ARRAY_SIZE(crystal_imgs),
    [NICE_VIEW_THEME_LANDSCAPE]  = NICE_VIEW_ANIM_ARRAY_SIZE(landscape_imgs),
    [NICE_VIEW_THEME_EVANGELION] = NICE_VIEW_ANIM_ARRAY_SIZE(evangelion_imgs),
};
