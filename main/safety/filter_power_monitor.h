#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Minimum watts to treat the filter as running. */
#define FILTER_MIN_RUNNING_WATTS 1

typedef enum {
    FILTER_POWER_ZONE_UNKNOWN = 0,
    FILTER_POWER_ZONE_OFF,
    FILTER_POWER_ZONE_GREEN,
    FILTER_POWER_ZONE_YELLOW,
    FILTER_POWER_ZONE_RED,
} filter_power_zone_t;

esp_err_t filter_power_monitor_init(void);

bool filter_power_monitor_plug_is_online(void);

bool filter_power_monitor_get_watts(uint16_t *watts);

bool filter_power_monitor_is_filter_on(void);

bool filter_power_monitor_is_calibrated(void);

bool filter_power_monitor_get_baseline_watts(float *watts);

filter_power_zone_t filter_power_monitor_get_zone(void);

bool filter_power_monitor_is_normal(void);

bool filter_power_monitor_alarm_active(void);

/** Compute gauge scale range (W) from baseline and red cutoff band setting. */
bool filter_power_monitor_get_gauge_range(float *min_watts, float *max_watts);

/** @deprecated Use filter_power_monitor_get_gauge_range. */
bool filter_power_monitor_get_gauge_max_watts(float *max_watts);
