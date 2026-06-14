#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * AquaPilot talks to a companion ESP32 feeder over HTTP on the LAN.
 *
 * Feeder firmware API:
 *   GET  /api/status   -> device state and next feed time
 *   POST /api/feed     -> JSON body {"seconds": <number>} triggers a feed
 *   POST /api/schedule -> push feeding schedule from AquaPilot settings
 *   POST /api/skip     -> skip the next scheduled slot on the feeder
 */

esp_err_t feeder_client_init(void);

bool feeder_client_is_online(void);

/** True when the last status poll reported the feeder motor is running. */
bool feeder_client_is_feeding(void);

esp_err_t feeder_client_feed(uint16_t amount_tenths);
esp_err_t feeder_client_push_schedule(void);
esp_err_t feeder_client_skip_next(void);
bool feeder_client_get_status_json(char *buf, size_t len);

/** Request an immediate schedule push (e.g. after settings save). */
void feeder_client_request_schedule_push(void);

typedef struct {
    bool valid;
    bool feeding;
    uint16_t feed_tenths;
    uint32_t feed_elapsed_ms;
    uint32_t feed_duration_ms;
    uint32_t feed_steps;
} feeder_client_feed_status_t;

bool feeder_client_get_feed_status(feeder_client_feed_status_t *out);
void feeder_client_format_feed_status(const feeder_client_feed_status_t *status, char *buf, size_t len);
