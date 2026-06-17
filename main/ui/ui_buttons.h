#pragma once

#include "lvgl.h"

/** Remove LVGL default theme (blue primary + transitions) and apply flat button colors. */
void ui_style_flat_button(lv_obj_t *btn, uint32_t bg_hex, uint32_t pressed_bg_hex);

void ui_button_clear_pressed(lv_obj_t *btn);

/** Standard back button (140x48, LVGL primary blue theme). */
lv_obj_t *ui_create_back_button(lv_obj_t *parent, lv_event_cb_t cb);
