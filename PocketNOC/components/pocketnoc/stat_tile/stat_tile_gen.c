/**
 * @file stat_tile_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "stat_tile_gen.h"
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

lv_obj_t * stat_tile_create(lv_obj_t * parent, const char * caption, const char * value)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "stat_tile_#");
        lv_obj_set_width(lv_obj_0, 70);
        lv_obj_set_height(lv_obj_0, 46);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_radius(lv_obj_0, 10, 0);

        lv_obj_add_style(lv_obj_0, &style_pn_card, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, caption);
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_0, 4);
        lv_obj_set_style_text_font(lv_label_0, font_14, 0);
        lv_obj_set_style_text_color(lv_label_0, C_GREY, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_1, value);
        lv_obj_set_align(lv_label_1, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(lv_label_1, -4);
        lv_obj_set_style_text_font(lv_label_1, font_body, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

