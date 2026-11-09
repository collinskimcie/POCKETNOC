/**
 * @file service_detail_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "service_detail_gen.h"
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

lv_obj_t * service_detail_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "service_detail_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        page_header_create(lv_obj_0, "Service");

        lv_obj_t * lv_obj_1 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_1, 224);
        lv_obj_set_height(lv_obj_1, 100);
        lv_obj_set_align(lv_obj_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_obj_1, 8);
        lv_obj_set_y(lv_obj_1, 44);
        lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_radius(lv_obj_1, 14, 0);
        lv_obj_add_style(lv_obj_1, &style_pn_card, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_0, "Home server");
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_0, 12);
        lv_obj_set_y(lv_label_0, 8);
        lv_obj_set_width(lv_label_0, 200);
        lv_obj_set_height(lv_label_0, 20);
        lv_label_set_long_mode(lv_label_0, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(lv_label_0, font_body, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_1, "UP");
        lv_obj_set_align(lv_label_1, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_1, 12);
        lv_obj_set_y(lv_label_1, 28);
        lv_obj_set_style_text_font(lv_label_1, font_30, 0);
        lv_obj_set_style_text_color(lv_label_1, C_GREEN, 0);

        lv_obj_t * lv_label_2 = lv_label_create(lv_obj_1);
        lv_label_set_text(lv_label_2, "http://192.168.1.10:8080/health");
        lv_obj_set_align(lv_label_2, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_2, 12);
        lv_obj_set_y(lv_label_2, 64);
        lv_obj_set_width(lv_label_2, 200);
        lv_obj_set_height(lv_label_2, 32);
        lv_label_set_long_mode(lv_label_2, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(lv_label_2, font_14, 0);
        lv_obj_set_style_text_color(lv_label_2, C_GREY, 0);

        lv_obj_t * lv_obj_2 = lv_obj_create(lv_obj_0);
        lv_obj_set_width(lv_obj_2, 224);
        lv_obj_set_height(lv_obj_2, 74);
        lv_obj_set_align(lv_obj_2, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_obj_2, 8);
        lv_obj_set_y(lv_obj_2, 152);
        lv_obj_set_flag(lv_obj_2, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_radius(lv_obj_2, 14, 0);
        lv_obj_add_style(lv_obj_2, &style_pn_card, 0);
        lv_obj_t * lv_label_3 = lv_label_create(lv_obj_2);
        lv_label_set_text(lv_label_3, "Last: 41 ms (code 200)\nAverage: 44 ms (last 20)\nUptime: 99% (198 of 200)");
        lv_obj_set_align(lv_label_3, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_3, 12);
        lv_obj_set_y(lv_label_3, 8);
        lv_obj_set_width(lv_label_3, 200);
        lv_obj_set_style_text_font(lv_label_3, font_14, 0);
        lv_obj_set_style_text_line_space(lv_label_3, 4, 0);

        lv_obj_t * lv_label_4 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_4, "Response time (ms)");
        lv_obj_set_align(lv_label_4, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_label_4, 12);
        lv_obj_set_y(lv_label_4, 232);
        lv_obj_set_style_text_font(lv_label_4, font_14, 0);
        lv_obj_set_style_text_color(lv_label_4, C_GREY, 0);

        lv_obj_t * lv_chart_0 = lv_chart_create(lv_obj_0);
        lv_obj_set_width(lv_chart_0, 224);
        lv_obj_set_height(lv_chart_0, 60);
        lv_obj_set_align(lv_chart_0, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(lv_chart_0, 8);
        lv_obj_set_y(lv_chart_0, 252);
        lv_chart_set_type(lv_chart_0, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(lv_chart_0, 20);
        lv_chart_set_hor_div_line_count(lv_chart_0, 3);
        lv_chart_set_ver_div_line_count(lv_chart_0, 0);
        lv_obj_set_style_radius(lv_chart_0, 10, 0);
        lv_obj_set_style_bg_color(lv_chart_0, C_CHART_BG, 0);
        lv_obj_set_style_border_width(lv_chart_0, 0, 0);
        lv_obj_set_style_line_color(lv_chart_0, C_GRID, 0);
        lv_obj_set_style_width(lv_chart_0, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(lv_chart_0, 0, LV_PART_INDICATOR);
        lv_chart_set_axis_min_value(lv_chart_0, LV_CHART_AXIS_PRIMARY_Y, 0);
        lv_chart_set_axis_max_value(lv_chart_0, LV_CHART_AXIS_PRIMARY_Y, 500);
        lv_chart_series_t * lv_chart_series_0 = lv_chart_add_series(lv_chart_0, C_LIGHT_BLUE, LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t lv_chart_0_values_0[] = {42, 40, 45, 41, 44, 39, 43, 120, 46, 41, 40, 44, 43, 41, 45, 42, 40, 43, 41, 44};
        lv_chart_set_series_values(lv_chart_0, lv_chart_series_0, lv_chart_0_values_0, 20);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

