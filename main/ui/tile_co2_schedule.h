#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *schedule_label;
    lv_obj_t *countdown_label;
} tile_co2_schedule_t;

tile_co2_schedule_t tile_co2_schedule_create(lv_obj_t *parent);
void tile_co2_schedule_update(tile_co2_schedule_t *tile);
