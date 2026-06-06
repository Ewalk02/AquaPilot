#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
} tile_ambient_t;

tile_ambient_t tile_ambient_create(lv_obj_t *parent);

void tile_ambient_update(tile_ambient_t *tile);
