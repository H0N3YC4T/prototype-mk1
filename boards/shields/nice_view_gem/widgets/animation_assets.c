// widgets/animation_assets.c

#include <lvgl.h>
#include "animation.h"
#include "animation_assets.h"

#define NICE_VIEW_ANIM_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// TEMP (CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY): gate out the non-transmutation
// theme bitmaps to slim the peripheral build. Set the Kconfig to n to restore.
#if !CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY
// -------------------------------------------------------------------------------
// crystal theme (assets/animations/crystal.c)
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
    &crystal_01,
    &crystal_02,
    &crystal_03,
    &crystal_04,
    &crystal_05,
    &crystal_06,
    &crystal_07,
    &crystal_08,
    &crystal_09,
    &crystal_10,
    &crystal_11,
    &crystal_12,
    &crystal_13,
    &crystal_14,
    &crystal_15,
    &crystal_16,
};

// -------------------------------------------------------------------------------
// landscape theme (assets/animations/landscape.c)
LV_IMG_DECLARE(landscape_01);
LV_IMG_DECLARE(landscape_03);
LV_IMG_DECLARE(landscape_06);
LV_IMG_DECLARE(landscape_07);
LV_IMG_DECLARE(landscape_08);

static const lv_img_dsc_t *landscape_imgs[] = {
    &landscape_01,
    &landscape_03,
    &landscape_06,
    &landscape_07,
    &landscape_08,
};

// -------------------------------------------------------------------------------
// evangelion theme (assets/animations/evangelion.c)
LV_IMG_DECLARE(evangelion_02);
LV_IMG_DECLARE(evangelion_04);
LV_IMG_DECLARE(evangelion_05);
LV_IMG_DECLARE(evangelion_09);
LV_IMG_DECLARE(evangelion_10);
LV_IMG_DECLARE(evangelion_11);

static const lv_img_dsc_t *evangelion_imgs[] = {
    &evangelion_02,
    &evangelion_04,
    &evangelion_05,
    &evangelion_09,
    &evangelion_10,
    &evangelion_11,
};
#endif /* !CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY */

// -------------------------------------------------------------------------------
// transmutation theme (assets/animations/transmutation.c)
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
LV_IMG_DECLARE(transmutation_12);
LV_IMG_DECLARE(transmutation_13);
LV_IMG_DECLARE(transmutation_14);
LV_IMG_DECLARE(transmutation_15);
LV_IMG_DECLARE(transmutation_16);
LV_IMG_DECLARE(transmutation_17);
LV_IMG_DECLARE(transmutation_18);
LV_IMG_DECLARE(transmutation_19);
LV_IMG_DECLARE(transmutation_20);
LV_IMG_DECLARE(transmutation_21);
LV_IMG_DECLARE(transmutation_22);
LV_IMG_DECLARE(transmutation_23);
LV_IMG_DECLARE(transmutation_24);
LV_IMG_DECLARE(transmutation_25);
LV_IMG_DECLARE(transmutation_26);
LV_IMG_DECLARE(transmutation_27);
LV_IMG_DECLARE(transmutation_28);
LV_IMG_DECLARE(transmutation_29);
LV_IMG_DECLARE(transmutation_30);
LV_IMG_DECLARE(transmutation_31);
LV_IMG_DECLARE(transmutation_32);

static const lv_img_dsc_t *transmutation_imgs[] = {
    &transmutation_01,
    &transmutation_02,
    &transmutation_03,
    &transmutation_04,
    &transmutation_05,
    &transmutation_06,
    &transmutation_07,
    &transmutation_08,
    &transmutation_09,
    &transmutation_10,
    &transmutation_11,
    &transmutation_12,
    &transmutation_13,
    &transmutation_14,
    &transmutation_15,
    &transmutation_16,
    &transmutation_17,
    &transmutation_18,
    &transmutation_19,
    &transmutation_20,
    &transmutation_21,
    &transmutation_22,
    &transmutation_23,
    &transmutation_24,
    &transmutation_25,
    &transmutation_26,
    &transmutation_27,
    &transmutation_28,
    &transmutation_29,
    &transmutation_30,
    &transmutation_31,
    &transmutation_32,
};

#if !CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY
// -------------------------------------------------------------------------------
// omnissiah theme (assets/animations/omnissiah.c)
LV_IMG_DECLARE(omnissiah_01);
LV_IMG_DECLARE(omnissiah_02);
LV_IMG_DECLARE(omnissiah_03);
LV_IMG_DECLARE(omnissiah_04);
LV_IMG_DECLARE(omnissiah_05);
LV_IMG_DECLARE(omnissiah_06);
LV_IMG_DECLARE(omnissiah_07);
LV_IMG_DECLARE(omnissiah_08);
LV_IMG_DECLARE(omnissiah_09);
LV_IMG_DECLARE(omnissiah_10);
LV_IMG_DECLARE(omnissiah_11);
LV_IMG_DECLARE(omnissiah_12);
LV_IMG_DECLARE(omnissiah_13);
LV_IMG_DECLARE(omnissiah_14);
LV_IMG_DECLARE(omnissiah_15);
LV_IMG_DECLARE(omnissiah_16);
LV_IMG_DECLARE(omnissiah_17);
LV_IMG_DECLARE(omnissiah_18);
LV_IMG_DECLARE(omnissiah_19);
LV_IMG_DECLARE(omnissiah_20);
LV_IMG_DECLARE(omnissiah_21);
LV_IMG_DECLARE(omnissiah_22);
LV_IMG_DECLARE(omnissiah_23);
LV_IMG_DECLARE(omnissiah_24);
LV_IMG_DECLARE(omnissiah_25);
LV_IMG_DECLARE(omnissiah_26);
LV_IMG_DECLARE(omnissiah_27);
LV_IMG_DECLARE(omnissiah_28);
LV_IMG_DECLARE(omnissiah_29);
LV_IMG_DECLARE(omnissiah_30);
LV_IMG_DECLARE(omnissiah_31);
LV_IMG_DECLARE(omnissiah_32);

static const lv_img_dsc_t *omnissiah_imgs[] = {
    &omnissiah_01,
    &omnissiah_02,
    &omnissiah_03,
    &omnissiah_04,
    &omnissiah_05,
    &omnissiah_06,
    &omnissiah_07,
    &omnissiah_08,
    &omnissiah_09,
    &omnissiah_10,
    &omnissiah_11,
    &omnissiah_12,
    &omnissiah_13,
    &omnissiah_14,
    &omnissiah_15,
    &omnissiah_16,
    &omnissiah_17,
    &omnissiah_18,
    &omnissiah_19,
    &omnissiah_20,
    &omnissiah_21,
    &omnissiah_22,
    &omnissiah_23,
    &omnissiah_24,
    &omnissiah_25,
    &omnissiah_26,
    &omnissiah_27,
    &omnissiah_28,
    &omnissiah_29,
    &omnissiah_30,
    &omnissiah_31,
    &omnissiah_32,
};

// -------------------------------------------------------------------------------
// ultramar theme (assets/animations/ultramar.c)
LV_IMG_DECLARE(ultramar_01);
LV_IMG_DECLARE(ultramar_02);
LV_IMG_DECLARE(ultramar_03);
LV_IMG_DECLARE(ultramar_04);
LV_IMG_DECLARE(ultramar_05);
LV_IMG_DECLARE(ultramar_06);
LV_IMG_DECLARE(ultramar_07);
LV_IMG_DECLARE(ultramar_08);
LV_IMG_DECLARE(ultramar_09);
LV_IMG_DECLARE(ultramar_10);
LV_IMG_DECLARE(ultramar_11);
LV_IMG_DECLARE(ultramar_12);
LV_IMG_DECLARE(ultramar_13);
LV_IMG_DECLARE(ultramar_14);
LV_IMG_DECLARE(ultramar_15);
LV_IMG_DECLARE(ultramar_16);
LV_IMG_DECLARE(ultramar_17);
LV_IMG_DECLARE(ultramar_18);
LV_IMG_DECLARE(ultramar_19);
LV_IMG_DECLARE(ultramar_20);
LV_IMG_DECLARE(ultramar_21);
LV_IMG_DECLARE(ultramar_22);
LV_IMG_DECLARE(ultramar_23);
LV_IMG_DECLARE(ultramar_24);
LV_IMG_DECLARE(ultramar_25);
LV_IMG_DECLARE(ultramar_26);
LV_IMG_DECLARE(ultramar_27);
LV_IMG_DECLARE(ultramar_28);
LV_IMG_DECLARE(ultramar_29);
LV_IMG_DECLARE(ultramar_30);
LV_IMG_DECLARE(ultramar_31);
LV_IMG_DECLARE(ultramar_32);

static const lv_img_dsc_t *ultramar_imgs[] = {
    &ultramar_01,
    &ultramar_02,
    &ultramar_03,
    &ultramar_04,
    &ultramar_05,
    &ultramar_06,
    &ultramar_07,
    &ultramar_08,
    &ultramar_09,
    &ultramar_10,
    &ultramar_11,
    &ultramar_12,
    &ultramar_13,
    &ultramar_14,
    &ultramar_15,
    &ultramar_16,
    &ultramar_17,
    &ultramar_18,
    &ultramar_19,
    &ultramar_20,
    &ultramar_21,
    &ultramar_22,
    &ultramar_23,
    &ultramar_24,
    &ultramar_25,
    &ultramar_26,
    &ultramar_27,
    &ultramar_28,
    &ultramar_29,
    &ultramar_30,
    &ultramar_31,
    &ultramar_32,
};
#endif /* !CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY */

// -------------------------------------------------------------------------------
// Lookup Table
const lv_img_dsc_t * const *nice_view_anim_sets[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_TRANSMUTATION] = transmutation_imgs,
#if !CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY
    [NICE_VIEW_THEME_CRYSTAL]       = crystal_imgs,
    [NICE_VIEW_THEME_LANDSCAPE]     = landscape_imgs,
    [NICE_VIEW_THEME_EVANGELION]    = evangelion_imgs,
    [NICE_VIEW_THEME_OMNISSIAH]     = omnissiah_imgs,
    [NICE_VIEW_THEME_ULTRAMAR]      = ultramar_imgs,
#endif
};

// Frame Counter
const size_t nice_view_anim_lengths[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_TRANSMUTATION] = NICE_VIEW_ANIM_ARRAY_SIZE(transmutation_imgs),
#if !CONFIG_NICE_VIEW_GEM_TRANSMUTATION_ONLY
    [NICE_VIEW_THEME_CRYSTAL]       = NICE_VIEW_ANIM_ARRAY_SIZE(crystal_imgs),
    [NICE_VIEW_THEME_LANDSCAPE]     = NICE_VIEW_ANIM_ARRAY_SIZE(landscape_imgs),
    [NICE_VIEW_THEME_EVANGELION]    = NICE_VIEW_ANIM_ARRAY_SIZE(evangelion_imgs),
    [NICE_VIEW_THEME_OMNISSIAH]     = NICE_VIEW_ANIM_ARRAY_SIZE(omnissiah_imgs),
    [NICE_VIEW_THEME_ULTRAMAR]      = NICE_VIEW_ANIM_ARRAY_SIZE(ultramar_imgs),
#endif
};

// -------------------------------------------------------------------------------
// Per-theme per-frame dwell time (ms). Animations play fast/smooth; slideshows
// (landscape, evangelion) hold each image for several seconds.
const uint32_t nice_view_anim_frame_ms[NICE_VIEW_THEME_COUNT] = {
    [NICE_VIEW_THEME_CRYSTAL]       = 33,     // animation (~30 fps)
    [NICE_VIEW_THEME_LANDSCAPE]     = 15000,  // slideshow (15 s/frame)
    [NICE_VIEW_THEME_EVANGELION]    = 15000,  // slideshow (15 s/frame)
    [NICE_VIEW_THEME_TRANSMUTATION] = 66,     // animation (~15 fps)
    [NICE_VIEW_THEME_OMNISSIAH]     = 33,     // animation (~30 fps)
    [NICE_VIEW_THEME_ULTRAMAR]      = 33,     // animation (~30 fps)
};
