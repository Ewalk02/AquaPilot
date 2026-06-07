#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define TEMP_HISTORY_SLOTS        192
#define TEMP_HISTORY_INTERVAL_SEC (15 * 60)
#define TEMP_HISTORY_WINDOW_SEC   (48 * 3600)

esp_err_t temp_history_init(void);

bool temp_history_storage_ready(void);

uint16_t temp_history_point_count(void);

/**
 * Copy chart-ready values (°F as int32, LV_CHART_POINT_NONE when missing).
 * out_tank and out_ambient must hold at least temp_history_point_count() entries.
 */
bool temp_history_get_chart_values(int32_t *out_tank, int32_t *out_ambient, uint16_t max_points,
                                   int32_t *out_y_min, int32_t *out_y_max);

bool temp_history_logging_enabled(void);

void temp_history_clear(void);
