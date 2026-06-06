#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
    lv_obj_t *status_label;
} tile_temp_t;

/**
 * @brief Create a tank-temperature information tile inside parent.
 */
tile_temp_t tile_temp_create(lv_obj_t *parent);

/**
 * @brief Refresh the displayed temperature from temp_source.
 */
void tile_temp_update(tile_temp_t *tile);
