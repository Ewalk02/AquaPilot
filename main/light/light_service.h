#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t light_service_init(void);
void light_service_tick(void);

typedef enum {
    LIGHT_STATUS_UNKNOWN = 0,
    LIGHT_STATUS_MANUAL,
    LIGHT_STATUS_AUTO,
} light_status_mode_t;

bool light_service_is_on(void);
bool light_service_has_status(void);
light_status_mode_t light_service_get_mode(void);
uint8_t light_service_get_brightness_pct(void);
bool light_service_is_light_online(void);

#ifdef __cplusplus
}
#endif
