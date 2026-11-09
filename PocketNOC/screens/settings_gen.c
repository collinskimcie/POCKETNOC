/**
 * @file settings_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "settings_gen.h"
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

lv_obj_t * settings_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "settings_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        page_header_create(lv_obj_0, "Settings");

        lv_obj_t * setting_switch_row_0 = setting_switch_row_create(lv_obj_0, "Dark theme", &pn_dark_theme);
        lv_obj_set_x(setting_switch_row_0, 8);
        lv_obj_set_y(setting_switch_row_0, 49);

        lv_obj_t * setting_switch_row_1 = setting_switch_row_create(lv_obj_0, "Alerts", &pn_alerts);
        lv_obj_set_x(setting_switch_row_1, 8);
        lv_obj_set_y(setting_switch_row_1, 95);

        lv_obj_t * setting_switch_row_2 = setting_switch_row_create(lv_obj_0, "Buzzer mute", &pn_buzzer_mute);
        lv_obj_set_x(setting_switch_row_2, 8);
        lv_obj_set_y(setting_switch_row_2, 141);

        lv_obj_t * setting_button_row_0 = setting_button_row_create(lv_obj_0, "Auto-return", "90 s");
        lv_obj_set_x(setting_button_row_0, 8);
        lv_obj_set_y(setting_button_row_0, 187);

        lv_obj_t * setting_button_row_1 = setting_button_row_create(lv_obj_0, "Clock", "24 h");
        lv_obj_set_x(setting_button_row_1, 8);
        lv_obj_set_y(setting_button_row_1, 233);

        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, "Changing the theme restarts the board");
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_0, 10);
        lv_obj_set_y(lv_label_0, 288);
        lv_obj_set_style_text_font(lv_label_0, font_14, 0);
        lv_obj_set_style_text_color(lv_label_0, C_GREY, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

