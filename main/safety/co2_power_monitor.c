#include "co2_power_monitor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/lan_http.h"
#include "net/shelly_client.h"
#include "net/wifi_manager.h"
#include "safety/filter_calibration.h"
#include "schedule/co2_automation.h"
#include "schedule/co2_schedule.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "co2_power";

#define MONITOR_INTERVAL_MS 5000
#define POLL_PHASE_MS       1700
#define ALARM_DELAY_US      (60LL * 1000000LL)

static volatile bool s_alarm_active;
static volatile bool s_off_leak_alarm_active;
static volatile bool s_watts_valid;
static volatile bool s_switch_valid;
static volatile bool s_switch_on;
static volatile bool s_plug_online;
static volatile uint16_t s_cached_watts;
static int64_t s_alarm_condition_since_us;
static int64_t s_off_leak_condition_since_us;

static bool monitor_feature_enabled(void)
{
    bool enabled = false;
    return aquapilot_settings_get_co2_power_monitor_enabled(&enabled) && enabled;
}

static void refresh_status(void)
{
    if (filter_calibration_is_active()) {
        return;
    }

    if (lan_http_should_defer()) {
        return;
    }

    if (!aquapilot_wifi_is_connected()) {
        s_plug_online = false;
        return;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_CO2)) {
        s_plug_online = false;
        s_watts_valid = false;
        s_switch_valid = false;
        return;
    }

    shelly_plug_status_t status = {0};
    if (shelly_client_get_plug_status(AQUAPILOT_SHELLY_CO2, &status) == ESP_OK) {
        s_plug_online = true;
        s_watts_valid = status.watts_valid;
        s_switch_valid = status.output_valid;
        if (status.watts_valid) {
            s_cached_watts = status.watts;
        }
        if (status.output_valid) {
            s_switch_on = status.output_on;
        }
    } else {
        s_plug_online = false;
    }
}

static bool on_no_power_condition_met(void)
{
    if (!monitor_feature_enabled()) {
        return false;
    }

    if (!co2_schedule_is_injection_active()) {
        return false;
    }

    if (!s_watts_valid) {
        return false;
    }

    return s_cached_watts <= CO2_POWER_ALARM_WATTS_THRESHOLD;
}

static bool off_leak_condition_met(void)
{
    if (!monitor_feature_enabled()) {
        return false;
    }

    if (co2_schedule_is_injection_active()) {
        return false;
    }

    if (!s_watts_valid) {
        return false;
    }

    return s_cached_watts >= CO2_OFF_LEAK_WATTS_THRESHOLD;
}

static bool evaluate_debounced_alarm(bool condition, int64_t *since_us)
{
    if (!condition) {
        *since_us = 0;
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    if (*since_us == 0) {
        *since_us = now_us;
        return false;
    }

    return (now_us - *since_us) >= ALARM_DELAY_US;
}

static void handle_off_leak_correction(void)
{
    const esp_err_t err = shelly_client_plug_set(AQUAPILOT_SHELLY_CO2, false);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "CO2 off-window leak: forced plug OFF");
        co2_automation_sync_now();
    } else {
        ESP_LOGW(TAG, "CO2 off-window leak: force OFF failed (%s)", esp_err_to_name(err));
    }
}

static void monitor_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(POLL_PHASE_MS));

    while (true) {
        refresh_status();

        const bool on_no_power = evaluate_debounced_alarm(on_no_power_condition_met(), &s_alarm_condition_since_us);
        const bool off_leak_was_active = s_off_leak_alarm_active;
        s_alarm_active = on_no_power;
        s_off_leak_alarm_active =
            evaluate_debounced_alarm(off_leak_condition_met(), &s_off_leak_condition_since_us);

        if (s_off_leak_alarm_active && !off_leak_was_active) {
            handle_off_leak_correction();
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t co2_power_monitor_init(void)
{
    if (xTaskCreate(monitor_task, "co2_power", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CO2 power monitor started");
    return ESP_OK;
}

bool co2_power_monitor_alarm_active(void)
{
    return s_alarm_active;
}

bool co2_power_monitor_off_leak_alarm_active(void)
{
    return s_off_leak_alarm_active;
}

bool co2_power_monitor_get_watts(uint16_t *watts)
{
    if (watts == NULL || !s_watts_valid) {
        return false;
    }

    *watts = s_cached_watts;
    return true;
}

bool co2_power_monitor_plug_switch_on(bool *on)
{
    if (on == NULL || !s_switch_valid) {
        return false;
    }

    *on = s_switch_on;
    return true;
}

bool co2_power_monitor_plug_is_online(void)
{
    return s_plug_online;
}
