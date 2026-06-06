#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *leds[6];
} tile_connections_t;

tile_connections_t tile_connections_create(lv_obj_t *parent);
void tile_connections_update(tile_connections_t *tile);
