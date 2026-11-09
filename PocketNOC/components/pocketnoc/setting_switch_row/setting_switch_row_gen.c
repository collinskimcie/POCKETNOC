/**
 * @file setting_switch_row_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "setting_switch_row_gen.h"
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

lv_obj_t * setting_switch_row_create(lv_obj_t * parent, const char * text, lv_subject_t * subject)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "setting_switch_row_#");
        lv_obj_set_width(lv_obj_0, 224);
        lv_obj_set_height(lv_obj_0, 40);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_CLICKABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_card, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, text);
        lv_obj_set_align(lv_label_0, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_label_0, 12);
        lv_obj_set_style_text_font(lv_label_0, font_body, 0);

        lv_obj_t * lv_switch_0 = lv_switch_create(lv_obj_0);
        lv_obj_bind_checked(lv_switch_0, subject);
        lv_obj_set_width(lv_switch_0, 55);
        lv_obj_set_height(lv_switch_0, 30);
        lv_obj_set_align(lv_switch_0, LV_ALIGN_RIGHT_MID);
        lv_obj_set_x(lv_switch_0, -10);
        lv_obj_set_style_bg_color(lv_switch_0, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

