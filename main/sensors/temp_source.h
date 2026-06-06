#pragma once

#include <stdbool.h>

bool temp_source_has_reading(void);
float temp_source_get_tank_temp_f(void);
const char *temp_source_status_text(void);
