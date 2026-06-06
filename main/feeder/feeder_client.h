#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * AquaPilot talks to a companion ESP32 feeder over HTTP on the LAN.
 *
 * Feeder firmware API:
 *   GET  /api/status  -> 200 when reachable
 *   POST /api/feed    -> JSON body {"seconds": <uint>} triggers a feed
 */

esp_err_t feeder_client_init(void);

bool feeder_client_is_online(void);

esp_err_t feeder_client_feed(uint16_t seconds);
