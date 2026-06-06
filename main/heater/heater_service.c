#include "heater_service.h"

#include <stdio.h>

#include "ble/ble_central_manager.h"
#include "chihiros_ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater_console.h"
#include "net/wifi_manager.h"
#include "storage/aquapilot_settings.h"

#ifndef CONFIG_AQUAPILOT_HEATER_BLE_NAME_PREFIX
#define CONFIG_AQUAPILOT_HEATER_BLE_NAME_PREFIX "DYH1"
#endif

static const char *TAG = "heater_svc";

#define BLE_TICK_MS           1000
#define TEMP_POLL_INTERVAL_MS (15 * 60 * 1000)
#define WIFI_DEFER_TIMEOUT_MS 45000

static float s_temp_f;
static uint16_t s_power_watts;
static bool s_has_reading;
static bool s_wifi_sta_requested;
static esp_timer_handle_t s_poll_timer;
static esp_timer_handle_t s_wifi_defer_timer;
static float s_applied_setpoint_f = -1.0f;

static void apply_status(const chihiros_status_t *st)
{
    if (st == NULL || !st->status_valid || st->stale) {
        return;
    }

    s_temp_f = st->current_temp_f;
    s_power_watts = st->power_watts;
    s_has_reading = true;
    ESP_LOGI(TAG, "tank temp from heater: %.1f F (%u W%s)", s_temp_f, (unsigned)st->power_watts,
             st->heating ? ", heating" : "");
}

static void refresh_from_ble_status(void)
{
    chihiros_status_t st;
    if (chihiros_ble_get_status(&st)) {
        apply_status(&st);
    }
}

static void request_wifi_sta_if_needed(const char *reason)
{
    if (s_wifi_sta_requested) {
        return;
    }
    s_wifi_sta_requested = true;
    if (aquapilot_wifi_start_sta()) {
        ESP_LOGI(TAG, "Wi-Fi STA started (%s)", reason);
    } else {
        ESP_LOGI(TAG, "Wi-Fi STA deferred (%s)", reason);
    }
}

static void wifi_defer_timeout_cb(void *arg)
{
    (void)arg;
    request_wifi_sta_if_needed("heater setup timeout");
}

static void try_apply_saved_setpoint(void)
{
    float setpoint_f = 0.0f;
    if (!aquapilot_settings_has_heater_setpoint() ||
        !aquapilot_settings_get_heater_setpoint(&setpoint_f)) {
        s_applied_setpoint_f = -1.0f;
        return;
    }

    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st) || !st.connected || !st.subscribed) {
        return;
    }

    if (s_applied_setpoint_f == setpoint_f) {
        return;
    }

    esp_err_t err = chihiros_ble_set_target_f(setpoint_f);
    if (err == ESP_OK) {
        s_applied_setpoint_f = setpoint_f;
        ESP_LOGI(TAG, "heater setpoint %.1f F applied", setpoint_f);
    }
}

void heater_service_request_setpoint_apply(void)
{
    s_applied_setpoint_f = -1.0f;
    try_apply_saved_setpoint();
}

static void ble_tick_task(void *arg)
{
    (void)arg;

    while (true) {
        ble_central_manager_tick();
        refresh_from_ble_status();

        if (!s_wifi_sta_requested && chihiros_ble_has_valid_status()) {
            request_wifi_sta_if_needed("heater status received");
        }

        try_apply_saved_setpoint();

        vTaskDelay(pdMS_TO_TICKS(BLE_TICK_MS));
    }
}

static void temp_poll_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "scheduled temperature poll (15 min)");
    chihiros_ble_request_status_refresh();
}

static void on_startup_poll(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "startup temperature poll");
    chihiros_ble_request_status_refresh();
}

esp_err_t heater_service_init(void)
{
    chihiros_ble_set_name_prefix(CONFIG_AQUAPILOT_HEATER_BLE_NAME_PREFIX);

    esp_err_t err = chihiros_ble_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chihiros BLE start failed: %s", esp_err_to_name(err));
        return err;
    }

    heater_console_register();

    if (xTaskCreate(ble_tick_task, "ble_tick", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create ble_tick task");
        return ESP_FAIL;
    }

    const esp_timer_create_args_t poll_args = {
        .callback = temp_poll_cb,
        .name = "heater_poll",
    };
    err = esp_timer_create(&poll_args, &s_poll_timer);
    if (err != ESP_OK) {
        return err;
    }
    esp_timer_start_periodic(s_poll_timer, (uint64_t)TEMP_POLL_INTERVAL_MS * 1000ULL);

    const esp_timer_create_args_t startup_args = {
        .callback = on_startup_poll,
        .name = "heater_startup",
    };
    esp_timer_handle_t startup_timer;
    err = esp_timer_create(&startup_args, &startup_timer);
    if (err == ESP_OK) {
        /* After GATT setup + init; avoid refresh during connect (was restarting scan). */
        esp_timer_start_once(startup_timer, 20 * 1000 * 1000ULL);
    }

    const esp_timer_create_args_t wifi_defer_args = {
        .callback = wifi_defer_timeout_cb,
        .name = "wifi_defer",
    };
    err = esp_timer_create(&wifi_defer_args, &s_wifi_defer_timer);
    if (err == ESP_OK) {
        esp_timer_start_once(s_wifi_defer_timer, (uint64_t)WIFI_DEFER_TIMEOUT_MS * 1000ULL);
    }

    ESP_LOGI(TAG, "heater service started (prefix=%s, poll=%d min)", CONFIG_AQUAPILOT_HEATER_BLE_NAME_PREFIX,
             TEMP_POLL_INTERVAL_MS / 60000);
    return ESP_OK;
}

bool heater_service_get_temp_f(float *out_temp_f)
{
    if (!s_has_reading || out_temp_f == NULL) {
        return false;
    }
    *out_temp_f = s_temp_f;
    return true;
}

bool heater_service_get_power_watts(uint16_t *out_watts)
{
    if (!s_has_reading || out_watts == NULL) {
        return false;
    }
    *out_watts = s_power_watts;
    return true;
}

bool heater_service_has_reading(void)
{
    return s_has_reading;
}

bool heater_service_is_heater_online(void)
{
    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st)) {
        return false;
    }
    return st.connected && st.subscribed && st.status_valid && !st.stale;
}

bool heater_service_is_heater_off(void)
{
    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st) || !st.status_valid || st.stale) {
        return false;
    }
    return !st.heating;
}

const char *heater_service_source_text(void)
{
    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st)) {
        return "BLE starting…";
    }

    if (st.status_valid && !st.stale) {
        return "Chihiros heater";
    }
    if (st.stale) {
        return "Heater stale";
    }
    if (st.connected && st.subscribed) {
        return "Heater connected";
    }
    if (st.connected) {
        return "Heater pairing…";
    }
    if (ble_central_manager_is_ready()) {
        return "Scanning for heater…";
    }
    return "BLE starting…";
}
