#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *schedule_label;
    lv_obj_t *countdown_label;
} tile_feeder_status_t;

tile_feeder_status_t tile_feeder_status_create(lv_obj_t *parent);
void tile_feeder_status_update(tile_feeder_status_t *tile);
