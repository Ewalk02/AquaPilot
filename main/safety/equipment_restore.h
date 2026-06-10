#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "storage/aquapilot_settings.h"

typedef void (*equipment_status_cb_t)(const char *text);

/**
 * Apply normal equipment Shelly states: filter ON, CO2 per schedule, heater ON.
 * Returns true when every configured plug matches desired state (or is unconfigured).
 */
bool equipment_apply_normal_state(equipment_status_cb_t status_cb, bool use_step_delays);

/** Ensure one plug matches desired relay state; returns true on match or successful set. */
bool equipment_set_plug_desired(aquapilot_shelly_plug_t plug, bool desired);

esp_err_t equipment_restore_init(void);
