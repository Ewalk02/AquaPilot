#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
} tile_settings_t;

tile_settings_t tile_settings_create(lv_obj_t *parent);
