/**
 * @file info_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "info_gen.h"
#include "../PocketNOC.h"

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

lv_obj_t * info_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "info_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, "Device Info");
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_0, 6);
        lv_obj_set_style_text_font(lv_label_0, font_30, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_1, "Wi-Fi: MyNetwork\nIP: 192.168.1.50\nSignal: -62 dBm\nFree heap: 120 KB\nUptime: 3d 4h");
        lv_obj_set_align(lv_label_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_1, 8);
        lv_obj_set_y(lv_label_1, 46);
        lv_obj_set_width(lv_label_1, 224);
        lv_obj_set_style_text_font(lv_label_1, font_body, 0);
        lv_obj_set_style_text_line_space(lv_label_1, 4, 0);

        lv_obj_t * tab_bar_0 = tab_bar_create(lv_obj_0);
        lv_obj_set_align(tab_bar_0, LV_ALIGN_BOTTOM_MID);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

