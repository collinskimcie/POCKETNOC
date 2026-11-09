/**
 * @file tool_button_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "tool_button_gen.h"
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

lv_obj_t * tool_button_create(lv_obj_t * parent, const char * title, lv_color_t group_color)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_button_0 = lv_button_create(parent);
        lv_obj_set_name_static(lv_button_0, "tool_button_#");
        lv_obj_set_width(lv_button_0, 108);
        lv_obj_set_height(lv_button_0, 54);
        lv_obj_set_style_radius(lv_button_0, 12, 0);
        lv_obj_set_style_shadow_width(lv_button_0, 0, 0);
        lv_obj_set_style_pad_all(lv_button_0, 0, 0);
        lv_obj_set_style_bg_color(lv_button_0, C_CARD, 0);

        lv_obj_add_style(lv_button_0, &style_pn_pressed, LV_STATE_PRESSED);
        lv_obj_t * lv_obj_0 = lv_obj_create(lv_button_0);
        lv_obj_set_width(lv_obj_0, 4);
        lv_obj_set_height(lv_obj_0, 30);
        lv_obj_set_align(lv_obj_0, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_obj_0, 6);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_set_style_radius(lv_obj_0, 2, 0);
        lv_obj_set_style_border_width(lv_obj_0, 0, 0);
        lv_obj_set_style_bg_opa(lv_obj_0, (255 * 100 / 100), 0);
        lv_obj_set_style_bg_color(lv_obj_0, group_color, 0);

        lv_obj_t * lv_label_0 = lv_label_create(lv_button_0);
        lv_label_set_text(lv_label_0, title);
        lv_obj_set_align(lv_label_0, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_label_0, 16);
        lv_obj_set_width(lv_label_0, 86);
        lv_label_set_long_mode(lv_label_0, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_font(lv_label_0, font_14, 0);
        lv_obj_set_style_text_color(lv_label_0, C_TEXT, 0);

        the_root = lv_button_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

