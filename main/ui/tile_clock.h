#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
} tile_clock_t;

tile_clock_t tile_clock_create(lv_obj_t *parent);

void tile_clock_update(tile_clock_t *tile);
