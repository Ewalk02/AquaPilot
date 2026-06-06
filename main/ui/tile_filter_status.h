#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
} tile_filter_status_t;

tile_filter_status_t tile_filter_status_create(lv_obj_t *parent);
void tile_filter_status_update(tile_filter_status_t *tile);
