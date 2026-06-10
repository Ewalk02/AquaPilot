#include "co2_automation.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/shelly_client.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_calibration.h"
#include "schedule/co2_schedule.h"
#include "safety/maintenance_mode.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "co2_auto";

#define SYNC_INTERVAL_MS 30000

static int8_t s_last_desired = -1;

static void sync_co2_plug(void)
{
    if (filter_calibration_is_active() || maintenance_mode_is_active()) {
        return;
    }

    if (!co2_schedule_clock_ready()) {
        return;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_CO2)) {
        s_last_desired = -1;
        return;
    }

    const bool desired = co2_schedule_is_injection_active();
    bool actual_on = false;
    const bool know_actual = co2_power_monitor_plug_switch_on(&actual_on);

    if (know_actual && desired == actual_on) {
        s_last_desired = desired ? 1 : 0;
        return;
    }

    esp_err_t err = shelly_client_plug_set(AQUAPILOT_SHELLY_CO2, desired);
    if (err == ESP_OK) {
        s_last_desired = desired ? 1 : 0;
        ESP_LOGI(TAG, "CO2 plug set %s", desired ? "ON" : "OFF");
    } else if (!know_actual) {
        ESP_LOGW(TAG, "CO2 plug set %s failed (relay state unknown): %s", desired ? "ON" : "OFF",
                 esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "CO2 plug set %s failed (relay was %s): %s", desired ? "ON" : "OFF",
                 actual_on ? "ON" : "OFF", esp_err_to_name(err));
    }
}

static void automation_task(void *arg)
{
    (void)arg;

    sync_co2_plug();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(SYNC_INTERVAL_MS));
        sync_co2_plug();
    }
}

void co2_automation_sync_now(void)
{
    s_last_desired = -1;
    sync_co2_plug();
}

esp_err_t co2_automation_init(void)
{
    if (xTaskCreate(automation_task, "co2_auto", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create automation task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CO2 schedule automation started");
    return ESP_OK;
}
