/**
 * @file incidents_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "incidents_gen.h"
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

lv_obj_t * incidents_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "incidents_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        page_header_create(lv_obj_0, "Incidents");

        lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_1, 224);
        lv_obj_set_height(lv_obj_1, 264);
        lv_obj_set_align(lv_obj_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_obj_1, 8);
        lv_obj_set_y(lv_obj_1, 48);
        lv_obj_set_style_pad_all(lv_obj_1, 8, 0);
        lv_obj_add_style(lv_obj_1, &style_pn_card, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_0, "12:40 Website DOWN\n12:52 Website UP (12 min)\n09:15 DNS DOWN\n09:16 DNS UP (1 min)");
        lv_obj_set_width(lv_label_0, 208);
        lv_label_set_long_mode(lv_label_0, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_font(lv_label_0, font_body, 0);
        lv_obj_set_style_text_line_space(lv_label_0, 3, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

