#pragma once

#include <stdbool.h>

void aquapilot_temp_range_init(void);
bool aquapilot_temp_range_get(float *min_f, float *max_f);
bool aquapilot_temp_range_set(float min_f, float max_f);
bool aquapilot_temp_range_contains(float temp_f);
