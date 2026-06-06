#pragma once

#include "lvgl.h"

void tile_apply_panel_style(lv_obj_t *obj);
lv_obj_t *tile_empty_create(lv_obj_t *parent);

/** Large centered alarm text; hides the label when text is NULL or empty. */
void tile_show_alarm_message(lv_obj_t *label, const char *text, uint32_t color_hex);

/** Restore a primary tile value label after an alarm clears. */
void tile_restore_value_label(lv_obj_t *label, uint32_t color_hex);

