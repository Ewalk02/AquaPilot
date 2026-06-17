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
    MAINT_BUILTIN_COUNT,
} maintenance_builtin_t;

#define MAINT_CUSTOM_MAX 8
#define MAINT_ACTIVITY_COUNT (MAINT_BUILTIN_COUNT + MAINT_CUSTOM_MAX)

typedef int maintenance_activity_t;

typedef enum {
    MAINT_TILE_SEVERITY_GREEN = 0,
    MAINT_TILE_SEVERITY_YELLOW,
    MAINT_TILE_SEVERITY_RED,
    MAINT_TILE_SEVERITY_UNKNOWN,
} maintenance_tile_severity_t;

static inline bool maintenance_is_builtin(maintenance_activity_t id)
{
    return id >= 0 && id < MAINT_BUILTIN_COUNT;
}

static inline bool maintenance_is_custom(maintenance_activity_t id)
{
    return id >= MAINT_BUILTIN_COUNT && id < MAINT_ACTIVITY_COUNT;
}

static inline int maintenance_custom_slot(maintenance_activity_t id)
{
    return id - MAINT_BUILTIN_COUNT;
}

esp_err_t maintenance_tracker_init(void);

int maintenance_activity_count(void);
bool maintenance_custom_is_active(maintenance_activity_t id);
int maintenance_custom_slots_available(void);
const char *maintenance_activity_label(maintenance_activity_t id);

/** Days until due; negative = overdue. Returns INT32_MIN when time is unknown. */
int maintenance_days_until_due(maintenance_activity_t id);

maintenance_tile_severity_t maintenance_tile_severity(void);

/** Fill up to three soonest-due activities (most urgent first). */
void maintenance_get_top3(maintenance_activity_t out_ids[3], int out_days[3]);

bool maintenance_complete(maintenance_activity_t id);
bool maintenance_delay_one_week(maintenance_activity_t id);

int maintenance_interval_days(maintenance_activity_t id);
bool maintenance_set_interval_days(maintenance_activity_t id, int days);

/** Returns activity id on success, or -1 when full/invalid. */
int maintenance_add_custom(const char *name, int interval_days);

void maintenance_format_due_text(maintenance_activity_t id, char *buf, size_t len);
