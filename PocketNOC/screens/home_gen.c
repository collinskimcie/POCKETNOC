/**
 * @file home_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "home_gen.h"
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

lv_obj_t * home_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "home_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, "PocketNOC");
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_0, 12);
        lv_obj_set_y(lv_label_0, 9);
        lv_obj_set_style_text_font(lv_label_0, font_14, 0);
        lv_obj_set_style_text_color(lv_label_0, C_GREY, 0);

        lv_obj_t * lv_image_0 = lv_image_create(lv_obj_0);
        lv_image_set_src(lv_image_0, icon_wifi);
        lv_obj_set_align(lv_image_0, LV_ALIGN_TOP_RIGHT);
        lv_obj_set_x(lv_image_0, -46);
        lv_obj_set_y(lv_image_0, 10);
        lv_obj_set_style_image_recolor(lv_image_0, C_TEXT, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_0, (255 * 100 / 100), 0);

        lv_obj_t * lv_button_0 = lv_button_create(lv_obj_0);
        lv_obj_set_width(lv_button_0, 34);
        lv_obj_set_height(lv_button_0, 28);
        lv_obj_set_align(lv_button_0, LV_ALIGN_TOP_RIGHT);
        lv_obj_set_x(lv_button_0, -4);
        lv_obj_set_y(lv_button_0, 3);
        lv_obj_set_style_bg_opa(lv_button_0, (255 * 0 / 100), 0);
        lv_obj_set_style_shadow_width(lv_button_0, 0, 0);
        lv_obj_set_style_pad_all(lv_button_0, 0, 0);
        lv_obj_t * lv_image_1 = lv_image_create(lv_button_0);
        lv_image_set_src(lv_image_1, icon_settings);
        lv_obj_set_align(lv_image_1, LV_ALIGN_CENTER);
        lv_obj_set_style_image_recolor(lv_image_1, C_GREY, 0);
        lv_obj_set_style_image_recolor_opa(lv_image_1, (255 * 100 / 100), 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_1, "14:05");
        lv_obj_set_align(lv_label_1, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_1, 26);
        lv_obj_set_style_text_font(lv_label_1, font_30, 0);

        lv_obj_t * lv_label_2 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_2, "Thu 5 Nov");
        lv_obj_set_align(lv_label_2, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_2, 62);
        lv_obj_set_style_text_font(lv_label_2, font_14, 0);
        lv_obj_set_style_text_color(lv_label_2, C_GREY, 0);

        lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_1, 224);
        lv_obj_set_height(lv_obj_1, 92);
        lv_obj_set_align(lv_obj_1, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_obj_1, 84);
        lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_radius(lv_obj_1, 14, 0);
        lv_obj_add_style(lv_obj_1, &style_pn_card, 0);
        lv_obj_t * lv_arc_0 = lv_arc_create(lv_obj_1);
        lv_obj_set_width(lv_arc_0, 72);
        lv_obj_set_height(lv_arc_0, 72);
        lv_obj_set_align(lv_arc_0, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_arc_0, 10);
        lv_obj_set_flag(lv_arc_0, LV_OBJ_FLAG_CLICKABLE, false);
        lv_arc_set_min_value(lv_arc_0, 0);
        lv_arc_set_max_value(lv_arc_0, 100);
        lv_arc_set_value(lv_arc_0, 80);
        lv_arc_set_bg_start_angle(lv_arc_0, 0);
        lv_arc_set_bg_end_angle(lv_arc_0, 360);
        lv_arc_set_rotation(lv_arc_0, 270);
        lv_obj_set_style_arc_width(lv_arc_0, 9, 0);
        lv_obj_set_style_arc_width(lv_arc_0, 9, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(lv_arc_0, C_BG, 0);
        lv_obj_set_style_arc_color(lv_arc_0, C_BLUE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(lv_arc_0, (255 * 0 / 100), LV_PART_KNOB);
        lv_obj_set_style_pad_all(lv_arc_0, 0, LV_PART_KNOB);

        lv_obj_t * lv_label_3 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_3, "4/5");
        lv_obj_set_align(lv_label_3, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_label_3, 10);
        lv_obj_set_width(lv_label_3, 72);
        lv_obj_set_style_text_align(lv_label_3, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lv_label_3, font_body, 0);

        lv_obj_t * lv_label_4 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_4, "1 service down");
        lv_obj_set_align(lv_label_4, LV_ALIGN_LEFT_MID);
        lv_obj_set_x(lv_label_4, 96);
        lv_obj_set_width(lv_label_4, 118);
        lv_label_set_long_mode(lv_label_4, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_font(lv_label_4, font_body, 0);

        lv_obj_t * stat_tile_0 = stat_tile_create(lv_obj_0, "Avg", "142 ms");
        lv_obj_set_x(stat_tile_0, 8);
        lv_obj_set_y(stat_tile_0, 180);

        lv_obj_t * stat_tile_1 = stat_tile_create(lv_obj_0, "Signal", "-62 dBm");
        lv_obj_set_x(stat_tile_1, 85);
        lv_obj_set_y(stat_tile_1, 180);

        lv_obj_t * stat_tile_2 = stat_tile_create(lv_obj_0, "Uptime", "3d 4h");
        lv_obj_set_x(stat_tile_2, 162);
        lv_obj_set_y(stat_tile_2, 180);

        lv_obj_t * quick_button_0 = quick_button_create(lv_obj_0, icon_wifi, "Scan", C_CARD);
        lv_obj_set_x(quick_button_0, 4);
        lv_obj_set_y(quick_button_0, 230);

        lv_obj_t * quick_button_1 = quick_button_create(lv_obj_0, icon_refresh, "Ping", C_CARD);
        lv_obj_set_x(quick_button_1, 51);
        lv_obj_set_y(quick_button_1, 230);

        lv_obj_t * quick_button_2 = quick_button_create(lv_obj_0, icon_download, "Speed", C_CARD);
        lv_obj_set_x(quick_button_2, 98);
        lv_obj_set_y(quick_button_2, 230);

        lv_obj_t * quick_button_3 = quick_button_create(lv_obj_0, icon_check, "Check", C_CARD);
        lv_obj_set_x(quick_button_3, 145);
        lv_obj_set_y(quick_button_3, 230);

        lv_obj_t * quick_button_4 = quick_button_create(lv_obj_0, icon_star, "Git", C_ACCENT);
        lv_obj_set_x(quick_button_4, 192);
        lv_obj_set_y(quick_button_4, 230);

        lv_obj_t * tab_bar_0 = tab_bar_create(lv_obj_0);
        lv_obj_set_align(tab_bar_0, LV_ALIGN_BOTTOM_MID);

        lv_obj_t * alarm_banner_0 = alarm_banner_create(lv_obj_0, "DOWN: Example service");
        lv_obj_set_align(alarm_banner_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(alarm_banner_0, 4);
        lv_obj_set_flag(alarm_banner_0, LV_OBJ_FLAG_HIDDEN, true);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

