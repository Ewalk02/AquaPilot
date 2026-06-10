#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t light_service_init(void);

/** Request a short BLE session to read light status (also used after heater polls). */
void light_service_request_poll(void);
void light_service_tick(void);

typedef enum {
    LIGHT_STATUS_UNKNOWN = 0,
    LIGHT_STATUS_MANUAL,
    LIGHT_STATUS_AUTO,
} light_status_mode_t;

bool light_service_is_on(void);
/** True when a recent BLE reading is available (not stale). */
bool light_service_status_is_known(void);
bool light_service_has_status(void);
light_status_mode_t light_service_get_mode(void);
uint8_t light_service_get_brightness_pct(void);
bool light_service_is_light_online(void);

#ifdef __cplusplus
}
#endif
