#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t heater_service_init(void);

/** Latest valid heater temperature in °F, or false if none yet. */
bool heater_service_get_temp_f(float *out_temp_f);

/** Latest valid heater power in watts, or false if none yet. */
bool heater_service_get_power_watts(uint16_t *out_watts);

bool heater_service_has_reading(void);
bool heater_service_is_heater_online(void);

/** True when Chihiros reports the heater element is off. */
bool heater_service_is_heater_off(void);

/** Human-readable source line for the temperature tile. */
const char *heater_service_source_text(void);

/** Apply persisted heater setpoint when BLE is ready (no-op if unchanged or unavailable). */
void heater_service_request_setpoint_apply(void);
