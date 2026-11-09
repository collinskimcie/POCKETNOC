/**
 * @file status_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "status_gen.h"
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

lv_obj_t * status_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if POCKETNOC_CHECK_COMPILE_TARGET(POCKETNOC_TARGET_ALL)
    if (PocketNOC_check_target(POCKETNOC_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "status_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_pn_page, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_0, "Services");
        lv_obj_set_align(lv_label_0, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_0, 6);
        lv_obj_set_style_text_font(lv_label_0, font_30, 0);

        lv_obj_t * service_row_0 = service_row_create(lv_obj_0, "Router", "ping 3 ms", C_GREEN);
        lv_obj_set_x(service_row_0, 8);
        lv_obj_set_y(service_row_0, 46);

        lv_obj_t * service_row_1 = service_row_create(lv_obj_0, "Home server", "200 OK  41 ms", C_GREEN);
        lv_obj_set_x(service_row_1, 8);
        lv_obj_set_y(service_row_1, 87);

        lv_obj_t * service_row_2 = service_row_create(lv_obj_0, "DNS", "12 ms", C_GREEN);
        lv_obj_set_x(service_row_2, 8);
        lv_obj_set_y(service_row_2, 128);

        lv_obj_t * service_row_3 = service_row_create(lv_obj_0, "GitHub", "200 OK  180 ms", C_GREEN);
        lv_obj_set_x(service_row_3, 8);
        lv_obj_set_y(service_row_3, 169);

        lv_obj_t * service_row_4 = service_row_create(lv_obj_0, "Website", "timeout", C_RED);
        lv_obj_set_x(service_row_4, 8);
        lv_obj_set_y(service_row_4, 210);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_text(lv_label_1, "4 of 5 up");
        lv_obj_set_align(lv_label_1, LV_ALIGN_TOP_MID);
        lv_obj_set_y(lv_label_1, 252);
        lv_obj_set_style_text_font(lv_label_1, font_body, 0);

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

