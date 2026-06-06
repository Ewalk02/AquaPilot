#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "storage/aquapilot_settings.h"

esp_err_t shelly_client_plug_set(aquapilot_shelly_plug_t plug, bool on);

/** Read active power (W) from a Shelly plug. Returns ESP_OK when a reading is parsed. */
esp_err_t shelly_client_get_plug_power_watts(aquapilot_shelly_plug_t plug, uint16_t *watts);

/** Turn off the configured heater Shelly plug (heater safety cutoff). */
esp_err_t shelly_client_heater_plug_off(void);

/**
 * During filter calibration, block all non-filter Shelly traffic and disable
 * inter-request pacing so calibration can poll every 2 s unimpeded.
 */
void shelly_client_set_exclusive_filter_mode(bool enabled);

bool shelly_client_is_exclusive_filter_mode(void);
