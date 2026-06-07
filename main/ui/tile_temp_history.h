#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *chart;
    lv_chart_series_t *tank_series;
    lv_chart_series_t *ambient_series;
    lv_obj_t *y_max_label;
    lv_obj_t *y_mid_label;
    lv_obj_t *y_min_label;
} tile_temp_history_t;

tile_temp_history_t tile_temp_history_create(lv_obj_t *parent);
void tile_temp_history_update(tile_temp_history_t *tile);
