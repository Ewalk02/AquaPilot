#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define CO2_POWER_ALARM_WATTS_THRESHOLD 1

esp_err_t co2_power_monitor_init(void);

/** True when schedule expects CO2 on but plug draws no power. */
bool co2_power_monitor_alarm_active(void);

/** Latest Shelly CO2 plug power reading, when available. */
bool co2_power_monitor_get_watts(uint16_t *watts);

/** True when the CO2 Shelly plug responded on the last poll. */
bool co2_power_monitor_plug_is_online(void);
