#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
    lv_obj_t *brightness_label;
} tile_light_status_t;

tile_light_status_t tile_light_status_create(lv_obj_t *parent);
void tile_light_status_update(tile_light_status_t *tile);
