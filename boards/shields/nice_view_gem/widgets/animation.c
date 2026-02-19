#include <stdlib.h>
#include <zephyr/kernel.h>
#include "animation.h"

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

static const lv_img_dsc_t *anim_imgs[] = {
    &transmutation_09, &transmutation_08, &transmutation_02, &transmutation_01,
    &transmutation_03, &transmutation_10, &transmutation_04, &transmutation_11, 
    &transmutation_05, &transmutation_06, &transmutation_07,
};


void draw_animation(lv_obj_t *canvas) {
#if IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
    lv_obj_t *art = lv_animimg_create(canvas);
    lv_obj_center(art);

    lv_animimg_set_src(art, (const void **)anim_imgs, 11);
    lv_animimg_set_duration(art, CONFIG_NICE_VIEW_GEM_ANIMATION_MS);
    lv_animimg_set_repeat_count(art, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(art);
#else
    lv_obj_t *art = lv_img_create(canvas);

    int length = sizeof(anim_imgs) / sizeof(anim_imgs[0]);
    srand(k_uptime_get_32());
    int random_index = rand() % length;
    int configured_index = (CONFIG_NICE_VIEW_GEM_ANIMATION_FRAME - 1) % length;
    int anim_imgs_index = CONFIG_NICE_VIEW_GEM_ANIMATION_FRAME > 0 ? configured_index : random_index;

    lv_img_set_src(art, anim_imgs[anim_imgs_index]);
#endif

    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 36, 0);
}