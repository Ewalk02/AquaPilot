#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *gauge_container;
    lv_obj_t *value_label;
    lv_obj_t *bar_row;
    lv_obj_t *marker;
    lv_obj_t *scale_labels;
    lv_obj_t *hint_label;
} tile_filter_watts_t;

tile_filter_watts_t tile_filter_watts_create(lv_obj_t *parent);
void tile_filter_watts_update(tile_filter_watts_t *tile);

/** Faster refresh while filter is running and calibrated. */
bool tile_filter_watts_needs_fast_update(void);
