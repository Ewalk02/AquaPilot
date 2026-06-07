#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

/** Minimum quiet time between LAN HTTP transactions (Shelly, feeder, etc.). */
#define LAN_HTTP_MIN_GAP_US (2500000LL)

/** True while BLE light poll window owns the radio — defer new LAN HTTP. */
bool lan_http_should_defer(void);

/** Block until BLE is idle or max_wait elapses. */
bool lan_http_wait_until_ready(TickType_t max_wait_ticks);

/** Pace requests; pass skip_gap during filter calibration bursts. */
void lan_http_pace(bool skip_gap);

bool lan_http_acquire(TickType_t timeout_ticks);
void lan_http_release(void);
