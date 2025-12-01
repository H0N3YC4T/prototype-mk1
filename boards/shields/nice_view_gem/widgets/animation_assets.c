// widgets/animation_assets.c

#include <lvgl.h>
#include "animation.h"
#include "animation_assets.h"

#define NICE_VIEW_ANIM_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// -------------------------------------------------------------------------------
// Declare Crystal Animation
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
// Declare Transmutation Animation
LV_IMG_DECLARE(transmutation_01);
LV_IMG_DECLARE(transmutation_02);
LV_IMG_DECLARE(transmutation_03);
LV_IMG_DECLARE(transmutation_04);
LV_IMG_DECLARE(transmutation_05);
LV_IMG_DECLARE(transmutation_06);
LV_IMG_DECLARE(transmutation_07);
LV_IMG_DECLARE(transmutation_08);
LV_IMG_DECLARE(transmutation_09);
LV_IMG_DECLARE(transmutation_10);
LV_IMG_DECLARE(transmutation_11);

static const lv_img_dsc_t *transmutation_imgs[] = {
    &transmutation_09, &transmutation_08, &transmutation_02, &transmutation_09, 
    &transmutation_03, &transmutation_10, &transmutation_04, &transmutation_11, 
    &transmutation_05, &transmutation_06, &transmutation_07, &transmutation_01,
};

// -------------------------------------------------------------------------------
// Lookup Table
const lv_img_dsc_t * const *nice_view_anim_sets[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_TRANSMUTATION] = transmutation_imgs,
    [NICE_VIEW_THEME_CRYSTAL]       = crystal_imgs,
};

// Frame Counter
const size_t nice_view_anim_lengths[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_TRANSMUTATION] = NICE_VIEW_ANIM_ARRAY_SIZE(transmutation_imgs),
    [NICE_VIEW_THEME_CRYSTAL]       = NICE_VIEW_ANIM_ARRAY_SIZE(crystal_imgs),
};