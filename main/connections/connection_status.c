#include "connection_status.h"

#include "ble/ble_central_manager.h"
#include "heater/heater_service.h"
#include "net/wifi_manager.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_power_monitor.h"

bool connection_status_is_on(connection_id_t id)
{
    switch (id) {
    case CONNECTION_WIFI:
        return aquapilot_wifi_is_connected();
    case CONNECTION_BLUETOOTH:
        return ble_central_manager_is_ready();
    case CONNECTION_HEATER:
        return heater_service_is_heater_online();
    case CONNECTION_CO2:
        return co2_power_monitor_plug_is_online();
    case CONNECTION_FILTER:
        return filter_power_monitor_plug_is_online();
    case CONNECTION_LIGHT:
    default:
        return false;
    }
}

const char *connection_status_label(connection_id_t id)
{
    switch (id) {
    case CONNECTION_WIFI:
        return "Wi-Fi";
    case CONNECTION_BLUETOOTH:
        return "Bluetooth";
    case CONNECTION_CO2:
        return "CO2";
    case CONNECTION_HEATER:
        return "Heater";
    case CONNECTION_FILTER:
        return "Filter";
    case CONNECTION_LIGHT:
        return "Light";
    default:
        return "?";
    }
}
