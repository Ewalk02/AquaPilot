#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *value_label;
    lv_obj_t *status_label;
} tile_heater_power_t;

/**
 * @brief Create a heater power monitor tile inside parent.
 */
tile_heater_power_t tile_heater_power_create(lv_obj_t *parent);

/**
 * @brief Refresh displayed watts from heater_service.
 */
void tile_heater_power_update(tile_heater_power_t *tile);

/** True while override alarm is active (use faster dashboard refresh). */
bool tile_heater_power_needs_fast_update(void);
