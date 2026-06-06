#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t sht3x_sensor_init(void);

bool sht3x_sensor_has_reading(void);

float sht3x_sensor_get_temp_f(void);

const char *sht3x_sensor_status_text(void);
