#include "heater_override.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater/heater_service.h"
#include "net/shelly_client.h"
#include "net/wifi_manager.h"
#include "safety/filter_calibration.h"
#include "safety/maintenance_mode.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "heater_override";

#define MONITOR_INTERVAL_MS      5000
#define POLL_PHASE_MS            3400
#define OFF_RETRY_INTERVAL_MS    30000

static volatile bool s_alarm_active;
static volatile heater_alarm_reason_t s_alarm_reason;
static int64_t s_last_off_attempt_ms;
static uint16_t s_cached_shelly_watts;
static bool s_cached_shelly_valid;

static bool refresh_heater_shelly_watts(void)
{
    if (filter_calibration_is_active()) {
        return false;
    }

    if (!heater_service_is_heater_off()) {
        return false;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_HEATER)) {
        return false;
    }

    if (!aquapilot_wifi_is_connected()) {
        return false;
    }

    uint16_t shelly_watts = 0;
    if (shelly_client_get_plug_power_watts(AQUAPILOT_SHELLY_HEATER, &shelly_watts) != ESP_OK) {
        return false;
    }

    s_cached_shelly_watts = shelly_watts;
    s_cached_shelly_valid = true;
    return true;
}

static bool shelly_mismatch_active(void)
{
    return s_cached_shelly_valid && s_cached_shelly_watts > HEATER_OVERRIDE_WATTS_THRESHOLD;
}

static bool evaluate_temp_alarm(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_heater_override_enabled(&enabled) || !enabled) {
        return false;
    }

    float temp_f = 0.0f;
    if (!heater_service_get_temp_f(&temp_f)) {
        return false;
    }

    float min_f = 0.0f;
    float max_f = 0.0f;
    if (!aquapilot_settings_get_temp_range(&min_f, &max_f)) {
        return false;
    }
    (void)min_f;

    uint16_t watts = 0;
    if (!heater_service_get_power_watts(&watts)) {
        return false;
    }

    return temp_f > max_f && watts > HEATER_OVERRIDE_WATTS_THRESHOLD;
}

static bool evaluate_shelly_power_alarm(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_heater_shelly_power_monitor_enabled(&enabled) || !enabled) {
        return false;
    }

    return shelly_mismatch_active();
}

static bool should_turn_off_shelly(void)
{
    if (evaluate_temp_alarm()) {
        return true;
    }

    if (!shelly_mismatch_active()) {
        return false;
    }

    bool override_enabled = false;
    bool monitor_enabled = false;
    aquapilot_settings_get_heater_override_enabled(&override_enabled);
    aquapilot_settings_get_heater_shelly_power_monitor_enabled(&monitor_enabled);
    return override_enabled || monitor_enabled;
}

static void maybe_turn_off_heater_plug(void)
{
    if (filter_calibration_is_active()) {
        return;
    }

    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (s_last_off_attempt_ms != 0 && (now_ms - s_last_off_attempt_ms) < OFF_RETRY_INTERVAL_MS) {
        return;
    }

    if (!aquapilot_wifi_is_connected()) {
        return;
    }

    s_last_off_attempt_ms = now_ms;
    esp_err_t err = shelly_client_heater_plug_off();
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Shelly heater plug turned off");
    } else {
        ESP_LOGW(TAG, "Shelly heater plug off failed: %s", esp_err_to_name(err));
    }
}

static void monitor_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(POLL_PHASE_MS));

    while (true) {
        if (maintenance_mode_is_active()) {
            s_alarm_active = false;
            s_alarm_reason = HEATER_ALARM_NONE;
            s_last_off_attempt_ms = 0;
            vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
            continue;
        }

        s_cached_shelly_valid = false;
        (void)refresh_heater_shelly_watts();

        heater_alarm_reason_t reason = HEATER_ALARM_NONE;

        if (evaluate_temp_alarm()) {
            reason = HEATER_ALARM_TEMP_HIGH;
        } else if (evaluate_shelly_power_alarm()) {
            reason = HEATER_ALARM_PLUG_ON;
        }

        s_alarm_reason = reason;
        s_alarm_active = reason != HEATER_ALARM_NONE;

        if (should_turn_off_shelly()) {
            maybe_turn_off_heater_plug();
        } else {
            s_last_off_attempt_ms = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t heater_override_init(void)
{
    if (xTaskCreate(monitor_task, "heater_override", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "heater override monitor started");
    return ESP_OK;
}

bool heater_override_alarm_active(void)
{
    return s_alarm_active;
}

heater_alarm_reason_t heater_override_alarm_reason(void)
{
    return s_alarm_reason;
}
