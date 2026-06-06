#include "co2_power_monitor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/shelly_client.h"
#include "net/wifi_manager.h"
#include "schedule/co2_schedule.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "co2_power";

#define MONITOR_INTERVAL_MS 2000
#define ALARM_DELAY_US      (60LL * 1000000LL)

static volatile bool s_alarm_active;
static volatile bool s_watts_valid;
static volatile bool s_plug_online;
static volatile uint16_t s_cached_watts;
static int64_t s_alarm_condition_since_us;

static void refresh_watts(void)
{
    if (!aquapilot_wifi_is_connected()) {
        s_plug_online = false;
        return;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_CO2)) {
        s_plug_online = false;
        s_watts_valid = false;
        return;
    }

    uint16_t watts = 0;
    if (shelly_client_get_plug_power_watts(AQUAPILOT_SHELLY_CO2, &watts) == ESP_OK) {
        s_cached_watts = watts;
        s_watts_valid = true;
        s_plug_online = true;
    } else {
        s_plug_online = false;
    }
}

static bool alarm_condition_met(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_co2_power_monitor_enabled(&enabled) || !enabled) {
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

static bool evaluate_alarm(void)
{
    if (!alarm_condition_met()) {
        s_alarm_condition_since_us = 0;
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_alarm_condition_since_us == 0) {
        s_alarm_condition_since_us = now_us;
        return false;
    }

    return (now_us - s_alarm_condition_since_us) >= ALARM_DELAY_US;
}

static void monitor_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (true) {
        refresh_watts();
        s_alarm_active = evaluate_alarm();
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

bool co2_power_monitor_get_watts(uint16_t *watts)
{
    if (watts == NULL || !s_watts_valid) {
        return false;
    }

    *watts = s_cached_watts;
    return true;
}

bool co2_power_monitor_plug_is_online(void)
{
    return s_plug_online;
}
