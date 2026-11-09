/**
 * @file tab_bar_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "tab_bar_gen.h"
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

lv_obj_t * tab_bar_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "tab_bar_#");
        lv_obj_set_width(lv_obj_0, 240);
        lv_obj_set_height(lv_obj_0, 40);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_radius(lv_obj_0, 0, 0);

        lv_obj_add_style(lv_obj_0, &style_pn_card, 0);
        lv_obj_t * lv_button_0 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_0, 60);
        lv_obj_set_height(lv_button_0, 40);
        lv_obj_set_x(lv_button_0, 0);
        lv_obj_set_y(lv_button_0, 0);
        lv_obj_set_style_radius(lv_button_0, 0, 0);
        lv_obj_set_style_pad_all(lv_button_0, 0, 0);
        lv_obj_set_style_shadow_width(lv_button_0, 0, 0);
        lv_obj_set_style_bg_opa(lv_button_0, (255 * 0 / 100), 0);
        lv_obj_t * lv_image_0 = lv_image_create(lv_button_0);
        lv_image_set_src(lv_image_0, icon_home);
        lv_obj_set_align(lv_image_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_image_0, 3);
        lv_obj_set_style_image_recolor(lv_image_0, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_0, (255 * 100 / 100), 0);

        lv_obj_t * lv_label_0 = lv_label_create(lv_button_0);
        lv_label_set_text(lv_label_0, "Home");
        lv_obj_set_align(lv_label_0, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(lv_label_0, -1);
        lv_obj_set_style_text_font(lv_label_0, font_14, 0);
        lv_obj_set_style_text_color(lv_label_0, C_TEXT, 0);

        lv_obj_t * lv_button_1 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_1, 60);
        lv_obj_set_height(lv_button_1, 40);
        lv_obj_set_x(lv_button_1, 60);
        lv_obj_set_y(lv_button_1, 0);
        lv_obj_set_style_radius(lv_button_1, 0, 0);
        lv_obj_set_style_pad_all(lv_button_1, 0, 0);
        lv_obj_set_style_shadow_width(lv_button_1, 0, 0);
        lv_obj_set_style_bg_opa(lv_button_1, (255 * 0 / 100), 0);
        lv_obj_t * lv_image_1 = lv_image_create(lv_button_1);
        lv_image_set_src(lv_image_1, icon_menu);
        lv_obj_set_align(lv_image_1, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_image_1, 3);
        lv_obj_set_style_image_recolor(lv_image_1, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_1, (255 * 100 / 100), 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_button_1);
        lv_label_set_text(lv_label_1, "Status");
        lv_obj_set_align(lv_label_1, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(lv_label_1, -1);
        lv_obj_set_style_text_font(lv_label_1, font_14, 0);
        lv_obj_set_style_text_color(lv_label_1, C_TEXT, 0);

        lv_obj_t * lv_button_2 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_2, 60);
        lv_obj_set_height(lv_button_2, 40);
        lv_obj_set_x(lv_button_2, 120);
        lv_obj_set_y(lv_button_2, 0);
        lv_obj_set_style_radius(lv_button_2, 0, 0);
        lv_obj_set_style_pad_all(lv_button_2, 0, 0);
        lv_obj_set_style_shadow_width(lv_button_2, 0, 0);
        lv_obj_set_style_bg_opa(lv_button_2, (255 * 0 / 100), 0);
        lv_obj_t * lv_image_2 = lv_image_create(lv_button_2);
        lv_image_set_src(lv_image_2, icon_settings);
        lv_obj_set_align(lv_image_2, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_image_2, 3);
        lv_obj_set_style_image_recolor(lv_image_2, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_2, (255 * 100 / 100), 0);

        lv_obj_t * lv_label_2 = lv_label_create(lv_button_2);
        lv_label_set_text(lv_label_2, "Tools");
        lv_obj_set_align(lv_label_2, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(lv_label_2, -1);
        lv_obj_set_style_text_font(lv_label_2, font_14, 0);
        lv_obj_set_style_text_color(lv_label_2, C_TEXT, 0);

        lv_obj_t * lv_button_3 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_3, 60);
        lv_obj_set_height(lv_button_3, 40);
        lv_obj_set_x(lv_button_3, 180);
        lv_obj_set_y(lv_button_3, 0);
        lv_obj_set_style_radius(lv_button_3, 0, 0);
        lv_obj_set_style_pad_all(lv_button_3, 0, 0);
        lv_obj_set_style_shadow_width(lv_button_3, 0, 0);
        lv_obj_set_style_bg_opa(lv_button_3, (255 * 0 / 100), 0);
        lv_obj_t * lv_image_3 = lv_image_create(lv_button_3);
        lv_image_set_src(lv_image_3, icon_info);
        lv_obj_set_align(lv_image_3, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_image_3, 3);
        lv_obj_set_style_image_recolor(lv_image_3, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_3, (255 * 100 / 100), 0);

        lv_obj_t * lv_label_3 = lv_label_create(lv_button_3);
        lv_label_set_text(lv_label_3, "Info");
        lv_obj_set_align(lv_label_3, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(lv_label_3, -1);
        lv_obj_set_style_text_font(lv_label_3, font_14, 0);
        lv_obj_set_style_text_color(lv_label_3, C_TEXT, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

