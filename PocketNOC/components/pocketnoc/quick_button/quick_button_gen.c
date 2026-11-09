/**
 * @file quick_button_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "quick_button_gen.h"
#include "../../../PocketNOC.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * quick_button_create(lv_obj_t * parent, const void * icon, const char * caption, lv_color_t bg_color)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_button_0 = lv_button_create(parent);
        lv_obj_set_name_static(lv_button_0, "quick_button_#");
        lv_obj_set_width(lv_button_0, 44);
        lv_obj_set_height(lv_button_0, 44);
        lv_obj_set_style_radius(lv_button_0, 10, 0);
        lv_obj_set_style_shadow_width(lv_button_0, 0, 0);
        lv_obj_set_style_pad_all(lv_button_0, 0, 0);
        lv_obj_set_style_bg_color(lv_button_0, bg_color, 0);

        lv_obj_add_style(lv_button_0, &style_pn_pressed, LV_STATE_PRESSED);
        lv_obj_t * lv_image_0 = lv_image_create(lv_button_0);
        lv_image_set_src(lv_image_0, icon);
        lv_obj_set_align(lv_image_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_image_0, 4);
        lv_obj_set_style_image_recolor(lv_image_0, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_0, (255 * 100 / 100), 0);

        lv_obj_t * lv_label_0 = lv_label_create(lv_button_0);
        lv_label_set_text(lv_label_0, caption);
        lv_obj_set_align(lv_label_0, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(lv_label_0, -3);
        lv_obj_set_style_text_font(lv_label_0, font_14, 0);
        lv_obj_set_style_text_color(lv_label_0, C_TEXT, 0);
        lv_obj_set_style_text_letter_space(lv_label_0, -1, 0);

        the_root = lv_button_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

