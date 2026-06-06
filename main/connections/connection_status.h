#pragma once

#include <stdbool.h>

typedef enum {
    CONNECTION_WIFI = 0,
    CONNECTION_BLUETOOTH,
    CONNECTION_CO2,
    CONNECTION_HEATER,
    CONNECTION_FILTER,
    CONNECTION_LIGHT,
    CONNECTION_FEEDER,
    CONNECTION_COUNT,
} connection_id_t;

/** @return true when the connection/device is active (green indicator). */
bool connection_status_is_on(connection_id_t id);

/** Short display label for each connection row. */
const char *connection_status_label(connection_id_t id);
