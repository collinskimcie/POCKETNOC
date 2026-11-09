/**
 * @file tools_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "tools_gen.h"
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

lv_obj_t * tools_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "tools_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, "Tools");
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_0, 6);
        lv_obj_set_style_text_font(lv_label_0, font_30, 0);

        lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_1, 228);
        lv_obj_set_height(lv_obj_1, 232);
        lv_obj_set_align(lv_obj_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_obj_1, 6);
        lv_obj_set_y(lv_obj_1, 44);
        lv_obj_set_flex_flow(lv_obj_1, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_bg_opa(lv_obj_1, (255 * 0 / 100), 0);
        lv_obj_set_style_border_width(lv_obj_1, 0, 0);
        lv_obj_set_style_pad_all(lv_obj_1, 2, 0);
        lv_obj_set_style_pad_gap(lv_obj_1, 6, 0);
        tool_group_heading_create(lv_obj_1, "Wi-Fi", C_LIGHT_BLUE);

        tool_button_create(lv_obj_1, "Scan networks", C_LIGHT_BLUE);

        tool_button_create(lv_obj_1, "Channels", C_LIGHT_BLUE);

        tool_button_create(lv_obj_1, "Signal", C_LIGHT_BLUE);

        tool_button_create(lv_obj_1, "Connect", C_LIGHT_BLUE);

        tool_group_heading_create(lv_obj_1, "LAN", C_GREEN);

        tool_button_create(lv_obj_1, "Ping", C_GREEN);

        tool_button_create(lv_obj_1, "Ports", C_GREEN);

        tool_button_create(lv_obj_1, "Wake-on-LAN", C_GREEN);

        tool_button_create(lv_obj_1, "New devices", C_GREEN);

        tool_group_heading_create(lv_obj_1, "Internet", C_ORANGE);

        tool_button_create(lv_obj_1, "Speed test", C_ORANGE);

        tool_button_create(lv_obj_1, "Public IP", C_ORANGE);

        tool_button_create(lv_obj_1, "DNS", C_ORANGE);

        tool_group_heading_create(lv_obj_1, "Cloud", C_PURPLE);

        tool_button_create(lv_obj_1, "GitHub", C_PURPLE);

        tool_group_heading_create(lv_obj_1, "IoT", C_TEAL);

        tool_button_create(lv_obj_1, "MQTT", C_TEAL);

        tool_button_create(lv_obj_1, "BLE", C_TEAL);

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

