#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    FILTER_CAL_IDLE = 0,
    FILTER_CAL_WAITING_POWER,
    FILTER_CAL_SAMPLING,
    FILTER_CAL_COMPLETE,
    FILTER_CAL_FAILED,
} filter_cal_state_t;

#define FILTER_CALIBRATION_DURATION_SEC 30
#define FILTER_CALIBRATION_MIN_WATTS    3
#define FILTER_CALIBRATION_POLL_MS      2000
#define FILTER_CALIBRATION_SAMPLE_COUNT ((FILTER_CALIBRATION_DURATION_SEC * 1000) / FILTER_CALIBRATION_POLL_MS)

esp_err_t filter_calibration_init(void);

void filter_calibration_start(void);

/** Abort an in-progress calibration. */
void filter_calibration_cancel(void);

bool filter_calibration_is_active(void);

filter_cal_state_t filter_calibration_get_state(void);

/** 0–100 while sampling; 0 otherwise. */
uint8_t filter_calibration_get_progress_pct(void);

/** Latest sample watts during calibration; false when unavailable. */
bool filter_calibration_get_current_watts(uint16_t *watts);

/** Running average watts during sampling; false when unavailable. */
bool filter_calibration_get_running_average(float *watts);

/** Final baseline after successful calibration; false when unavailable. */
bool filter_calibration_get_result_baseline(float *watts);

/** User-facing status line for the current state. */
const char *filter_calibration_get_message(void);
