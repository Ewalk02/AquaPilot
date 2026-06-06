#include "temp_source.h"

#include "heater/heater_service.h"

bool temp_source_has_reading(void)
{
    return heater_service_has_reading();
}

float temp_source_get_tank_temp_f(void)
{
    float temp_f = 0.0f;
    if (heater_service_get_temp_f(&temp_f)) {
        return temp_f;
    }
    return 0.0f;
}

const char *temp_source_status_text(void)
{
    return heater_service_source_text();
}
