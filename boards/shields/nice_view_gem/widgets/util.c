#include <zephyr/kernel.h>
#include <string.h>
#include <ctype.h>

#include "util.h"

void to_uppercase(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        str[i] = toupper((unsigned char)str[i]);
    }
}

void rotate_canvas(lv_obj_t *canvas, lv_color_t cbuf[]) {
    /* Make a temporary copy of the source buffer so we can safely draw back
     * into the canvas' own buffer.
     */
    static lv_color_t cbuf_tmp[BUFFER_SIZE * BUFFER_SIZE];
    memcpy(cbuf_tmp, cbuf, sizeof(cbuf_tmp));

    /* Describe the source image (pre-rotation) */
    lv_image_dsc_t img;
    memset(&img, 0, sizeof(img));
    img.header.w  = BUFFER_SIZE;
    img.header.h  = BUFFER_SIZE;
    img.header.cf = LV_COLOR_FORMAT_NATIVE;
    img.data      = (const uint8_t *)cbuf_tmp;
    img.data_size = sizeof(cbuf_tmp);

    /* Clear destination canvas */
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    /* Create a draw layer for the canvas and render the rotated image into it */
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src      = &img;                 // our tmp image as the source
    dsc.rotation = 900;                  // 90.0 degrees (0.1° units)
    dsc.pivot.x  = BUFFER_SIZE / 2;      // rotate around centre
    dsc.pivot.y  = BUFFER_SIZE / 2;

    lv_area_t coords = {
        .x1 = 0,
        .y1 = 0,
        .x2 = BUFFER_SIZE - 1,
        .y2 = BUFFER_SIZE - 1,
    };

    lv_draw_image(&layer, &dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);
}

void fill_background(lv_obj_t *canvas) {
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    lv_canvas_draw_rect(canvas, 0, 0, BUFFER_SIZE, BUFFER_SIZE, &rect_black_dsc);
}

void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color,
                    const lv_font_t *font, lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = color;
    label_dsc->font  = font;
    label_dsc->align = align;
}

void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color) {
    lv_draw_rect_dsc_init(rect_dsc);
    rect_dsc->bg_color = bg_color;
}

void init_line_dsc(lv_draw_line_dsc_t *line_dsc, lv_color_t color, uint8_t width) {
    lv_draw_line_dsc_init(line_dsc);
    line_dsc->color = color;
    line_dsc->width = width;
}
