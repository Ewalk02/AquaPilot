#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define HEATER_OVERRIDE_WATTS_THRESHOLD 10

typedef enum {
    HEATER_ALARM_NONE = 0,
    HEATER_ALARM_TEMP_HIGH,
    HEATER_ALARM_PLUG_ON,
} heater_alarm_reason_t;

esp_err_t heater_override_init(void);

/** True when a heater safety alarm is active (latched or current). */
bool heater_override_alarm_active(void);

/** Which heater alarm is active, if any. */
heater_alarm_reason_t heater_override_alarm_reason(void);

/** True when alarm persists after shutoff until normal conditions are restored. */
bool heater_override_alarm_is_latched(void);

/** Suppress Shelly power-mismatch shutoff briefly after equipment restore turns heater on. */
void heater_override_begin_post_restore_grace(void);
