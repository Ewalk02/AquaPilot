#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define AIR_POWER_ALARM_WATTS_THRESHOLD  5
#define AIR_OFF_LEAK_WATTS_THRESHOLD     2

esp_err_t air_power_monitor_init(void);

/** True when schedule expects air on but plug draws insufficient power. */
bool air_power_monitor_alarm_active(void);

/** True when schedule expects air off but plug draws significant power. */
bool air_power_monitor_off_leak_alarm_active(void);

/** Latest Shelly air pump plug power reading, when available. */
bool air_power_monitor_get_watts(uint16_t *watts);

/** Cached Shelly relay output from the last successful status poll. */
bool air_power_monitor_plug_switch_on(bool *on);

/** True when the air Shelly plug responded on the last poll. */
bool air_power_monitor_plug_is_online(void);
