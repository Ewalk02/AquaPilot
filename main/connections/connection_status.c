#include "connection_status.h"

#include "ble/ble_central_manager.h"
#include "esp_timer.h"
#include "heater/chihiros_ble.h"
#include "net/wifi_manager.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_power_monitor.h"
#include "feeder/feeder_client.h"

#define HEATER_DISPLAY_GRACE_MS 90000

typedef struct {
    bool led_on;
} ble_conn_led_state_t;

static ble_conn_led_state_t s_heater_led;
static uint32_t s_heater_last_success_ms;

static uint32_t conn_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool within_display_grace(uint32_t last_success_ms, uint32_t grace_ms)
{
    if (last_success_ms == 0) {
        return false;
    }

    return (conn_now_ms() - last_success_ms) < grace_ms;
}

static void mark_connected(ble_conn_led_state_t *st)
{
    st->led_on = true;
}

static bool heater_connection_led(void)
{
    chihiros_status_t st = {0};
    if (!chihiros_ble_get_status(&st)) {
        return within_display_grace(s_heater_last_success_ms, HEATER_DISPLAY_GRACE_MS) || s_heater_led.led_on;
    }

    const bool fresh_status = st.status_valid && !st.stale;

    if (st.connected || fresh_status) {
        s_heater_last_success_ms = conn_now_ms();
        mark_connected(&s_heater_led);
        return true;
    }

    if (within_display_grace(s_heater_last_success_ms, HEATER_DISPLAY_GRACE_MS)) {
        return true;
    }

    s_heater_led.led_on = false;
    return false;
}

bool connection_status_is_on(connection_id_t id)
{
    switch (id) {
    case CONNECTION_WIFI:
        return aquapilot_wifi_is_connected();
    case CONNECTION_BLUETOOTH:
        return ble_central_manager_is_ready();
    case CONNECTION_HEATER:
        return heater_connection_led();
    case CONNECTION_CO2:
        return co2_power_monitor_plug_is_online();
    case CONNECTION_FILTER:
        return filter_power_monitor_plug_is_online();
    case CONNECTION_FEEDER:
        return feeder_client_is_online();
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
    case CONNECTION_FEEDER:
        return "Feeder";
    default:
        return "?";
    }
}
