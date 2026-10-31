#pragma once
#include <Arduino.h>
#include <lvgl.h>

#define DEVICE_NAME "PocketNOC"   // shown on the screen and on the web pages
#define DEVICE_HOST "pocketnoc"   // reachable as http://pocketnoc.local/

void nettools_build_menu(lv_obj_t *tile);          // the Tools menu (goes on a tile)
void nettools_build_page(lv_obj_t *screen);        // the full-screen tool page (goes on the screen)
void nettools_set_config(const String &text);      // "key=value" lines from the web config page
void nettools_set_default_url(const char *url);    // first monitored service, used by HTTP timing

void nettools_open(const char *name);   // open a tool page by name (for the Home quick buttons)
void nettools_close();
bool nettools_is_open();
bool nettools_is_busy();
