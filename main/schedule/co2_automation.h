#pragma once

#include "esp_err.h"

esp_err_t co2_automation_init(void);

/** Re-apply CO2 schedule to the plug (e.g. after maintenance mode exits). */
void co2_automation_sync_now(void);
