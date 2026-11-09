/**
 * @file tool_group_heading_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "tool_group_heading_gen.h"
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

lv_obj_t * tool_group_heading_create(lv_obj_t * parent, const char * text, lv_color_t color)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_label_0 = lv_label_create(parent);
        lv_obj_set_name_static(lv_label_0, "tool_group_heading_#");
        lv_label_set_text(lv_label_0, text);
        lv_obj_set_width(lv_label_0, lv_pct(100));
        lv_obj_set_style_text_font(lv_label_0, font_body, 0);
        lv_obj_set_style_text_color(lv_label_0, color, 0);
        lv_obj_set_style_pad_top(lv_label_0, 8, 0);

        the_root = lv_label_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

