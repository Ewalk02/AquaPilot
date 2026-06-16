#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WATER_SAMPLE_MAX_RECORDS 64
#define WATER_SAMPLE_WINDOW_SEC  (90LL * 24 * 3600)

typedef enum {
    WATER_METRIC_PH = 0,
    WATER_METRIC_AMMONIA,
    WATER_METRIC_NITRATE,
    WATER_METRIC_NITRITE,
    WATER_METRIC_KH,
    WATER_METRIC_GH,
    WATER_METRIC_COUNT,
} water_sample_metric_t;

typedef struct {
    int32_t epoch;
    float ph;
    float ammonia_ppm;
    float nitrate_ppm;
    float nitrite_ppm;
    float kh_deg;
    float gh_deg;
} water_sample_record_t;

esp_err_t water_sample_history_init(void);

bool water_sample_history_storage_ready(void);

/** Retry SD mount and load/create the history file. Returns true when SD persistence is active. */
bool water_sample_history_try_attach_storage(void);

bool water_sample_history_add(const water_sample_record_t *record);

const char *water_sample_metric_label(water_sample_metric_t metric);

/** Copy up to max_points samples for one metric within WATER_SAMPLE_WINDOW_SEC. */
bool water_sample_history_get_metric_series(water_sample_metric_t metric, int32_t *out_epochs, float *out_values,
                                            uint16_t max_points, uint16_t *out_count);
