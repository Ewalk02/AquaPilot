#include "temp_range_config.h"

#include "aquapilot_settings.h"

void aquapilot_temp_range_init(void)
{
    /* Settings loaded once from app_main via aquapilot_settings_init(). */
}

bool aquapilot_temp_range_get(float *min_f, float *max_f)
{
    return aquapilot_settings_get_temp_range(min_f, max_f);
}

bool aquapilot_temp_range_set(float min_f, float max_f)
{
    (void)min_f;
    (void)max_f;
    return false;
}

bool aquapilot_temp_range_contains(float temp_f)
{
    return aquapilot_settings_temp_contains(temp_f);
}
