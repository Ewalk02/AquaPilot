#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t feeder_service_init(void);

bool feeder_service_is_enabled(void);
bool feeder_service_is_feeding(void);
bool feeder_service_is_ready(void);
const char *feeder_service_status_text(void);

void feeder_service_format_schedule_preview(char *buf, size_t len);

bool feeder_service_get_times_per_day(uint8_t *times_per_day);

void feeder_service_format_countdown(char *buf, size_t len);

esp_err_t feeder_service_feed_now(void);
esp_err_t feeder_service_skip_next_feeding(void);

void feeder_service_on_feed_complete(uint16_t amount_tenths, uint32_t steps);

/** True for 10 s after the feeder reports a completed feed. */
bool feeder_service_show_feed_complete(void);

/** True when a scheduled slot passed without a feeder completion callback. */
bool feeder_service_is_feed_missed(void);
