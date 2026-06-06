#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
    lv_obj_t *watts_label;
    lv_obj_t *status_label;
} tile_co2_status_t;

tile_co2_status_t tile_co2_status_create(lv_obj_t *parent);
void tile_co2_status_update(tile_co2_status_t *tile);

/** True while CO2 power alarm is active (use faster dashboard refresh). */
bool tile_co2_status_needs_fast_update(void);
