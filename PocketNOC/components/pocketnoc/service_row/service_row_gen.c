/**
 * @file service_row_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "service_row_gen.h"
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

lv_obj_t * service_row_create(lv_obj_t * parent, const char * title, const char * info, lv_color_t dot_color)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "service_row_#");
        lv_obj_set_width(lv_obj_0, 224);
        lv_obj_set_height(lv_obj_0, 40);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_card, 0);
        lv_obj_add_style(lv_obj_0, &style_pn_pressed, LV_STATE_PRESSED);
        lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_1, 14);
        lv_obj_set_height(lv_obj_1, 14);
        lv_obj_set_align(lv_obj_1, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_obj_1, 10);
        lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_set_style_radius(lv_obj_1, 100, 0);
        lv_obj_set_style_border_width(lv_obj_1, 0, 0);
        lv_obj_set_style_bg_opa(lv_obj_1, (255 * 100 / 100), 0);
        lv_obj_set_style_bg_color(lv_obj_1, dot_color, 0);

        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, title);
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_0, 32);
        lv_obj_set_y(lv_label_0, 3);
        lv_obj_set_width(lv_label_0, 180);
        lv_obj_set_height(lv_label_0, 20);
        lv_label_set_long_mode(lv_label_0, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(lv_label_0, font_body, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_1, info);
        lv_obj_set_align(lv_label_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_1, 32);
        lv_obj_set_y(lv_label_1, 22);
        lv_obj_set_style_text_font(lv_label_1, font_14, 0);
        lv_obj_set_style_text_color(lv_label_1, C_GREY, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

