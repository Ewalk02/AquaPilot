#include "connection_status.h"

#include "ble/ble_central_manager.h"
#include "esp_timer.h"
#include "fluval_ble.h"
#include "heater/chihiros_ble.h"
#include "net/wifi_manager.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_power_monitor.h"

#define HEATER_RECONNECT_WINDOW_US (45 * 1000 * 1000ULL)

typedef struct {
    bool led_on;
    uint8_t miss_streak;
    bool window_active;
    uint64_t window_start_us;
    bool connected_this_window;
} ble_conn_led_state_t;

static ble_conn_led_state_t s_heater_led;
static ble_conn_led_state_t s_light_led;
static bool s_heater_handoff_was_active;
static bool s_light_poll_was_active;

static bool heater_handoff_active(void)
{
    return ble_central_manager_is_light_exclusive() || fluval_ble_is_poll_window_active();
}

static void mark_connected(ble_conn_led_state_t *st)
{
    st->led_on = true;
    st->miss_streak = 0;
    st->window_active = false;
    st->connected_this_window = true;
}

static void close_window_as_miss(ble_conn_led_state_t *st)
{
    st->window_active = false;
    st->miss_streak++;
    if (st->miss_streak >= 2) {
        st->led_on = false;
    }
}

static bool heater_connection_led(void)
{
    chihiros_status_t st = {0};
    if (!chihiros_ble_get_status(&st)) {
        return s_heater_led.led_on;
    }

    const uint64_t now_us = esp_timer_get_time();
    const bool handoff = heater_handoff_active();

    if (st.connected) {
        mark_connected(&s_heater_led);
        s_heater_handoff_was_active = handoff;
        return true;
    }

    if (handoff) {
        s_heater_handoff_was_active = true;
        s_heater_led.window_active = false;
        return s_heater_led.led_on;
    }

    if (s_heater_handoff_was_active) {
        s_heater_handoff_was_active = false;
        s_heater_led.window_active = true;
        s_heater_led.window_start_us = now_us;
        s_heater_led.connected_this_window = false;
        return s_heater_led.led_on;
    }

    if (!s_heater_led.window_active) {
        s_heater_led.window_active = true;
        s_heater_led.window_start_us = now_us;
        s_heater_led.connected_this_window = false;
        return s_heater_led.led_on;
    }

    if (now_us - s_heater_led.window_start_us >= HEATER_RECONNECT_WINDOW_US) {
        close_window_as_miss(&s_heater_led);
        s_heater_led.window_active = true;
        s_heater_led.window_start_us = now_us;
        s_heater_led.connected_this_window = false;
    }

    return s_heater_led.led_on;
}

static bool light_connection_led(void)
{
    fluval_status_t st = {0};
    if (!fluval_ble_get_status(&st)) {
        return s_light_led.led_on;
    }

    const bool poll_active = fluval_ble_is_poll_window_active();
    const bool has_status = st.status_valid;

    if (st.connected || has_status) {
        mark_connected(&s_light_led);
        if (poll_active && has_status) {
            s_light_led.connected_this_window = true;
        }
        s_light_poll_was_active = poll_active;
        return true;
    }

    if (poll_active) {
        if (!s_light_poll_was_active) {
            s_light_led.connected_this_window = false;
        }
        s_light_poll_was_active = true;
        return s_light_led.led_on;
    }

    if (s_light_poll_was_active) {
        if (!s_light_led.connected_this_window) {
            close_window_as_miss(&s_light_led);
        }
        s_light_poll_was_active = false;
    }

    return s_light_led.led_on;
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
    case CONNECTION_LIGHT:
        return light_connection_led();
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
