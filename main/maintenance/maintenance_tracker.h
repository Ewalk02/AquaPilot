#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    MAINT_WATER_CHANGE = 0,
    MAINT_WATER_SAMPLING,
    MAINT_FILTER_CLEANING,
    MAINT_CHECK_CO2,
    MAINT_FILL_FEEDER,
    MAINT_ACTIVITY_COUNT,
} maintenance_activity_t;

typedef enum {
    MAINT_TILE_SEVERITY_GREEN = 0,
    MAINT_TILE_SEVERITY_YELLOW,
    MAINT_TILE_SEVERITY_RED,
    MAINT_TILE_SEVERITY_UNKNOWN,
} maintenance_tile_severity_t;

esp_err_t maintenance_tracker_init(void);

int maintenance_activity_count(void);
const char *maintenance_activity_label(maintenance_activity_t id);

/** Days until due; negative = overdue. Returns INT32_MIN when time is unknown. */
int maintenance_days_until_due(maintenance_activity_t id);

maintenance_tile_severity_t maintenance_tile_severity(void);

/** Fill up to three soonest-due activities (most urgent first). */
void maintenance_get_top3(maintenance_activity_t out_ids[3], int out_days[3]);

bool maintenance_complete(maintenance_activity_t id);
bool maintenance_delay_one_week(maintenance_activity_t id);

void maintenance_format_due_text(maintenance_activity_t id, char *buf, size_t len);
