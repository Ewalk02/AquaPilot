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
#include "safety/heater_override.h"
#include "safety/maintenance_mode.h"
#include "schedule/air_schedule.h"
#include "schedule/co2_automation.h"
#include "schedule/co2_schedule.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "equip_restore";

#define RESTORE_RETRY_INTERVAL_MS 45000
#define RESTORE_WINDOW_US         (15LL * 60LL * 1000000LL)
#define STEP_DELAY_MS             5000
#define EARLY_WIFI_DELAY_US       (10LL * 1000000LL)
#define FILTER_VERIFY_SETTLE_MS   2000

static SemaphoreHandle_t s_wake;
static int64_t s_retry_until_us;
static bool s_restore_complete;
static esp_timer_handle_t s_early_wifi_timer;

static void begin_retry_window(void);

static const char *plug_label(aquapilot_shelly_plug_t plug)
{
    switch (plug) {
    case AQUAPILOT_SHELLY_HEATER:
        return "heater";
    case AQUAPILOT_SHELLY_FILTER:
        return "filter";
    case AQUAPILOT_SHELLY_CO2:
        return "co2";
    case AQUAPILOT_SHELLY_AIR:
        return "air";
    default:
        return "unknown";
    }
}

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

static bool air_desired_state(bool *out_desired)
{
    if (out_desired == NULL) {
        return false;
    }

    if (!air_schedule_clock_ready()) {
        return false;
    }

    *out_desired = air_schedule_desired_plug_on();
    return true;
}

static bool filter_read_watts(uint16_t *watts_out)
{
    if (watts_out == NULL) {
        return false;
    }

    return shelly_client_get_plug_power_watts(AQUAPILOT_SHELLY_FILTER, watts_out) == ESP_OK;
}

static bool filter_watts_ok_for_heater(void)
{
    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
        return true;
    }

    uint16_t watts = 0;
    if (!filter_read_watts(&watts)) {
        ESP_LOGW(TAG, "filter watts read failed (heater gate)");
        return false;
    }

    if (watts < EQUIP_FILTER_MIN_HEATER_WATTS) {
        ESP_LOGW(TAG, "filter %u W below heater minimum %u W", (unsigned)watts,
                 (unsigned)EQUIP_FILTER_MIN_HEATER_WATTS);
        return false;
    }

    return true;
}

static bool filter_verify_power(bool desired_on)
{
    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
        return true;
    }

    uint16_t watts = 0;
    if (!filter_read_watts(&watts)) {
        ESP_LOGW(TAG, "filter watts read failed during verify");
        return false;
    }

    if (desired_on) {
        if (watts < EQUIP_FILTER_MIN_HEATER_WATTS) {
            ESP_LOGW(TAG, "filter ON verify failed: %u W (need >= %u W)", (unsigned)watts,
                     (unsigned)EQUIP_FILTER_MIN_HEATER_WATTS);
            return false;
        }
        ESP_LOGI(TAG, "filter ON verified: %u W", (unsigned)watts);
        return true;
    }

    if (watts >= EQUIP_FILTER_MIN_HEATER_WATTS) {
        ESP_LOGW(TAG, "filter OFF verify failed: still %u W", (unsigned)watts);
        return false;
    }

    ESP_LOGI(TAG, "filter OFF verified: %u W", (unsigned)watts);
    return true;
}

static void filter_settle_delay(bool needed)
{
    if (needed) {
        vTaskDelay(pdMS_TO_TICKS(FILTER_VERIFY_SETTLE_MS));
    }
}

static bool filter_force_relay_set(bool on)
{
    const esp_err_t err = shelly_client_plug_set(AQUAPILOT_SHELLY_FILTER, on);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "filter relay force %s failed: %s", on ? "ON" : "OFF", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "filter relay forced %s", on ? "ON" : "OFF");
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

    const char *label = plug_label(plug);
    bool relay_matches = false;

    shelly_plug_status_t status = {0};
    if (shelly_client_get_plug_status(plug, &status) == ESP_OK && status.output_valid &&
        status.output_on == desired) {
        relay_matches = true;
        if (plug != AQUAPILOT_SHELLY_FILTER) {
            ESP_LOGI(TAG, "plug %s already %s, skipping set", label, desired ? "ON" : "OFF");
            return true;
        }
        ESP_LOGI(TAG, "plug %s already %s, verifying watts", label, desired ? "ON" : "OFF");
    }

    bool sent_command = false;
    if (!relay_matches) {
        const esp_err_t err = shelly_client_plug_set(plug, desired);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "plug %s set %s failed: %s", label, desired ? "ON" : "OFF", esp_err_to_name(err));
            return false;
        }

        ESP_LOGI(TAG, "plug %s set %s", label, desired ? "ON" : "OFF");
        sent_command = true;
    }

    if (plug != AQUAPILOT_SHELLY_FILTER) {
        return true;
    }

    filter_settle_delay(sent_command);
    if (filter_verify_power(desired)) {
        return true;
    }

    if (!desired) {
        return false;
    }

    ESP_LOGW(TAG, "filter relay/watts mismatch, forcing relay ON");
    if (!filter_force_relay_set(true)) {
        return false;
    }

    filter_settle_delay(true);
    return filter_verify_power(true);
}

bool equipment_apply_normal_state(equipment_status_cb_t status_cb, bool use_step_delays,
                                  bool allow_during_maintenance)
{
    if (!allow_during_maintenance && maintenance_mode_is_active()) {
        ESP_LOGW(TAG, "restore skipped: maintenance mode active");
        return false;
    }

    if (!aquapilot_wifi_is_connected()) {
        ESP_LOGW(TAG, "restore skipped: Wi-Fi offline");
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

    bool air_on = false;
    if (air_desired_state(&air_on)) {
        if (air_on) {
            notify_status(status_cb, "Turning on air pump plug (schedule active)...");
        } else {
            notify_status(status_cb, "Air pump plug off (outside schedule)...");
        }
        if (!equipment_set_plug_desired(AQUAPILOT_SHELLY_AIR, air_on)) {
            all_ok = false;
        }
    } else if (aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_AIR)) {
        all_ok = false;
    }
    delay_step(use_step_delays);

    if (!filter_watts_ok_for_heater()) {
        notify_status(status_cb, "Heater waiting for filter...");
        ESP_LOGW(TAG, "heater skipped: filter not running (need >= %u W)",
                 (unsigned)EQUIP_FILTER_MIN_HEATER_WATTS);
        all_ok = false;
    } else {
        notify_status(status_cb, "Turning on heater plug...");
        if (!equipment_set_plug_desired(AQUAPILOT_SHELLY_HEATER, true)) {
            all_ok = false;
        } else {
            heater_override_begin_post_restore_grace();
        }
    }

    if (all_ok) {
        co2_automation_sync_now();
        ESP_LOGI(TAG, "restore complete");
    } else {
        ESP_LOGW(TAG, "restore incomplete: one or more plugs failed");
    }

    return all_ok;
}

void equipment_restore_request_retry(void)
{
    ESP_LOGI(TAG, "restore retry requested");
    begin_retry_window();
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
        if (equipment_apply_normal_state(NULL, false, false)) {
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
