#include "equipment_restore.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/shelly_client.h"
#include "net/wifi_manager.h"
#include "safety/filter_calibration.h"
#include "safety/maintenance_mode.h"
#include "schedule/co2_automation.h"
#include "schedule/co2_schedule.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "equip_restore";

#define RESTORE_RETRY_INTERVAL_MS 45000
#define RESTORE_WINDOW_US         (15LL * 60LL * 1000000LL)
#define STEP_DELAY_MS             5000
#define EARLY_WIFI_DELAY_US       (10LL * 1000000LL)

static SemaphoreHandle_t s_wake;
static int64_t s_retry_until_us;
static bool s_restore_complete;
static esp_timer_handle_t s_early_wifi_timer;

static void notify_status(equipment_status_cb_t status_cb, const char *text)
{
    if (status_cb != NULL && text != NULL) {
        status_cb(text);
    }
}

static void delay_step(bool use_step_delays)
{
    if (use_step_delays) {
        vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
    }
}

static bool co2_desired_state(bool *out_desired)
{
    if (out_desired == NULL) {
        return false;
    }

    if (!co2_schedule_clock_ready()) {
        return false;
    }

    *out_desired = co2_schedule_is_injection_active();
    return true;
}

bool equipment_set_plug_desired(aquapilot_shelly_plug_t plug, bool desired)
{
    if (!aquapilot_settings_has_shelly_address(plug)) {
        return true;
    }

    if (!aquapilot_wifi_is_connected()) {
        return false;
    }

    shelly_plug_status_t status = {0};
    if (shelly_client_get_plug_status(plug, &status) == ESP_OK && status.output_valid &&
        status.output_on == desired) {
        return true;
    }

    const esp_err_t err = shelly_client_plug_set(plug, desired);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "plug %d set %s failed: %s", (int)plug, desired ? "ON" : "OFF", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "plug %d set %s", (int)plug, desired ? "ON" : "OFF");
    return true;
}

bool equipment_apply_normal_state(equipment_status_cb_t status_cb, bool use_step_delays)
{
    if (maintenance_mode_is_active()) {
        return false;
    }

    if (!aquapilot_wifi_is_connected()) {
        return false;
    }

    bool all_ok = true;

    notify_status(status_cb, "Turning on filter plug...");
    if (!equipment_set_plug_desired(AQUAPILOT_SHELLY_FILTER, true)) {
        all_ok = false;
    }
    delay_step(use_step_delays);

    bool co2_on = false;
    if (co2_desired_state(&co2_on)) {
        if (co2_on) {
            notify_status(status_cb, "Turning on CO2 plug (schedule active)...");
        } else {
            notify_status(status_cb, "CO2 plug off (outside schedule)...");
        }
        if (!equipment_set_plug_desired(AQUAPILOT_SHELLY_CO2, co2_on)) {
            all_ok = false;
        }
    } else if (aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_CO2)) {
        all_ok = false;
    }
    delay_step(use_step_delays);

    notify_status(status_cb, "Turning on heater plug...");
    if (!equipment_set_plug_desired(AQUAPILOT_SHELLY_HEATER, true)) {
        all_ok = false;
    }

    if (all_ok) {
        co2_automation_sync_now();
    }

    return all_ok;
}

static void begin_retry_window(void)
{
    s_retry_until_us = esp_timer_get_time() + RESTORE_WINDOW_US;
    s_restore_complete = false;
    if (s_wake != NULL) {
        xSemaphoreGive(s_wake);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi up — starting equipment restore window");
    begin_retry_window();
}

static void early_wifi_cb(void *arg)
{
    (void)arg;

    if (aquapilot_wifi_start_sta()) {
        ESP_LOGI(TAG, "Wi-Fi STA started (early boot)");
    }
}

static void restore_task(void *arg)
{
    (void)arg;

    begin_retry_window();

    while (true) {
        if (xSemaphoreTake(s_wake, pdMS_TO_TICKS(RESTORE_RETRY_INTERVAL_MS)) != pdTRUE) {
            /* Periodic retry interval elapsed. */
        }

        if (maintenance_mode_is_active() || maintenance_mode_sequence_running()) {
            continue;
        }

        if (filter_calibration_is_active()) {
            continue;
        }

        const int64_t now_us = esp_timer_get_time();
        if (s_restore_complete && now_us > s_retry_until_us) {
            continue;
        }

        if (!aquapilot_wifi_is_connected()) {
            continue;
        }

        ESP_LOGI(TAG, "applying normal equipment state");
        if (equipment_apply_normal_state(NULL, false)) {
            ESP_LOGI(TAG, "equipment restore complete");
            s_restore_complete = true;
        } else if (now_us > s_retry_until_us) {
            ESP_LOGW(TAG, "equipment restore window ended with mismatches remaining");
        }
    }
}

esp_err_t equipment_restore_init(void)
{
    if (s_wake == NULL) {
        s_wake = xSemaphoreCreateBinary();
        if (s_wake == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IP event register failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_timer_create_args_t early_wifi_args = {
        .callback = early_wifi_cb,
        .name = "wifi_early",
    };
    err = esp_timer_create(&early_wifi_args, &s_early_wifi_timer);
    if (err == ESP_OK) {
        esp_timer_start_once(s_early_wifi_timer, EARLY_WIFI_DELAY_US);
    } else {
        ESP_LOGW(TAG, "early Wi-Fi timer create failed");
    }

    if (xTaskCreate(restore_task, "equip_restore", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "restore task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "equipment restore started");
    return ESP_OK;
}
