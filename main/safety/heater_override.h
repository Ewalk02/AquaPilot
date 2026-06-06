#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define HEATER_OVERRIDE_WATTS_THRESHOLD 5

typedef enum {
    HEATER_ALARM_NONE = 0,
    HEATER_ALARM_TEMP_HIGH,
    HEATER_ALARM_PLUG_ON,
} heater_alarm_reason_t;

esp_err_t heater_override_init(void);

/** True when any heater safety alarm is active. */
bool heater_override_alarm_active(void);

/** Which heater alarm is active, if any. */
heater_alarm_reason_t heater_override_alarm_reason(void);
