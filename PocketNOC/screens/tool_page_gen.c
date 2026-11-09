/**
 * @file tool_page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "tool_page_gen.h"
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

lv_obj_t * tool_page_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "tool_page_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        lv_obj_t * lv_button_0 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_0, 84);
        lv_obj_set_height(lv_button_0, 30);
        lv_obj_set_align(lv_button_0, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_button_0, 6);
        lv_obj_set_y(lv_button_0, 4);
        lv_obj_set_style_radius(lv_button_0, 10, 0);
        lv_obj_set_style_shadow_width(lv_button_0, 0, 0);
        lv_obj_set_style_bg_color(lv_button_0, C_CARD, 0);
        lv_obj_add_style(lv_button_0, &style_pn_pressed, LV_STATE_PRESSED);
        lv_obj_t * lv_label_0 = lv_label_create(lv_button_0);
        lv_label_set_text(lv_label_0, "< Back");
        lv_obj_set_align(lv_label_0, LV_ALIGN_CENTER);
        lv_obj_set_style_text_color(lv_label_0, C_TEXT, 0);

        lv_obj_t * lv_button_1 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_1, 84);
        lv_obj_set_height(lv_button_1, 30);
        lv_obj_set_align(lv_button_1, LV_ALIGN_TOP_RIGHT);
        lv_obj_set_x(lv_button_1, -6);
        lv_obj_set_y(lv_button_1, 4);
        lv_obj_set_style_radius(lv_button_1, 10, 0);
        lv_obj_set_style_shadow_width(lv_button_1, 0, 0);
        lv_obj_set_style_bg_color(lv_button_1, C_ACCENT, 0);
        lv_obj_add_style(lv_button_1, &style_pn_pressed, LV_STATE_PRESSED);
        lv_obj_t * lv_label_1 = lv_label_create(lv_button_1);
        lv_label_set_text(lv_label_1, "Run");
        lv_obj_set_align(lv_label_1, LV_ALIGN_CENTER);
        lv_obj_set_style_text_color(lv_label_1, lv_color_hex(0xffffff), 0);

        lv_obj_t * lv_label_2 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_2, "Ping");
        lv_obj_set_align(lv_label_2, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_2, 6);
        lv_obj_set_y(lv_label_2, 38);
        lv_obj_set_width(lv_label_2, 228);
        lv_obj_set_height(lv_label_2, 20);
        lv_label_set_long_mode(lv_label_2, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(lv_label_2, font_body, 0);

        lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_1, 228);
        lv_obj_set_height(lv_obj_1, 206);
        lv_obj_set_align(lv_obj_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_obj_1, 6);
        lv_obj_set_y(lv_obj_1, 62);
        lv_obj_set_style_pad_all(lv_obj_1, 6, 0);
        lv_obj_add_style(lv_obj_1, &style_pn_card, 0);
        lv_obj_t * lv_label_3 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_3, "Reply from 192.168.1.1: 3 ms\nReply from 192.168.1.1: 2 ms\nReply from 192.168.1.1: 3 ms");
        lv_obj_set_width(lv_label_3, 208);
        lv_obj_set_style_text_font(lv_label_3, font_body, 0);
        lv_obj_set_style_text_line_space(lv_label_3, 3, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

