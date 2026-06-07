#include "light_service.h"

#include "ble/ble_central_manager.h"
#include "fluval_ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater/heater_service.h"
#include "light_console.h"
#include "chihiros_ble.h"

#ifndef CONFIG_AQUAPILOT_LIGHT_BLE_NAME_PREFIX
#define CONFIG_AQUAPILOT_LIGHT_BLE_NAME_PREFIX "Plant4.0"
#endif

static const char *TAG = "light_svc";

#define LIGHT_POLL_INTERVAL_US   (120 * 1000 * 1000ULL)
#define LIGHT_POLL_MIN_UPTIME_US (45 * 1000 * 1000ULL)
#define BLE_TICK_MS              2000

static uint8_t s_brightness_pct;
static bool s_has_status;
static bool s_is_on;
static light_status_mode_t s_mode;

static void apply_status(const fluval_status_t *st)
{
    if (st == NULL || !st->status_valid) {
        return;
    }

    s_brightness_pct = st->avg_output;
    s_has_status = true;
    s_is_on = st->avg_output > 0;
    if (st->mode == FLUVAL_MODE_AUTO) {
        s_mode = LIGHT_STATUS_AUTO;
    } else if (st->mode == FLUVAL_MODE_MANUAL) {
        s_mode = LIGHT_STATUS_MANUAL;
    } else {
        s_mode = LIGHT_STATUS_UNKNOWN;
    }
    ESP_LOGI(TAG, "light output %u%% (%s)", (unsigned)st->avg_output,
             st->mode == FLUVAL_MODE_AUTO ? "auto" : "manual");
}

static void refresh_from_ble_status(void)
{
    fluval_status_t st;
    if (fluval_ble_get_status(&st)) {
        apply_status(&st);
    }
}

static bool heater_ready_for_light_poll(void)
{
    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st)) {
        return false;
    }
    return st.connected && st.subscribed && st.status_valid && !st.stale;
}

void light_service_tick(void)
{
    static uint64_t last_poll_us;
    static uint64_t heater_stable_since_us;

    refresh_from_ble_status();

    const uint64_t now_us = esp_timer_get_time();
    if (heater_ready_for_light_poll()) {
        if (heater_stable_since_us == 0) {
            heater_stable_since_us = now_us;
        }
    } else {
        heater_stable_since_us = 0;
        return;
    }

    if (now_us - heater_stable_since_us < LIGHT_POLL_MIN_UPTIME_US) {
        return;
    }
    if (now_us - last_poll_us < LIGHT_POLL_INTERVAL_US) {
        return;
    }

    last_poll_us = now_us;
    ESP_LOGI(TAG, "requesting light poll window");
    fluval_ble_request_poll_window();
}

static void light_ble_tick_task(void *arg)
{
    (void)arg;

    while (true) {
        ble_central_manager_tick();
        light_service_tick();
        vTaskDelay(pdMS_TO_TICKS(BLE_TICK_MS));
    }
}

esp_err_t light_service_init(void)
{
    fluval_ble_set_name_prefix(CONFIG_AQUAPILOT_LIGHT_BLE_NAME_PREFIX);

    esp_err_t err = fluval_ble_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fluval BLE start failed");
        return err;
    }

    if (!heater_service_ble_task_is_running()) {
        if (xTaskCreate(light_ble_tick_task, "ble_tick", 4096, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "failed to create ble_tick task");
            return ESP_FAIL;
        }
    }

    light_console_register();
    ESP_LOGI(TAG, "light service ready");
    return ESP_OK;
}

bool light_service_is_on(void)
{
    fluval_status_t st;
    if (!fluval_ble_get_status(&st) || !st.status_valid || st.stale) {
        return s_has_status && s_is_on;
    }
    return st.avg_output > 0;
}

bool light_service_has_status(void)
{
    fluval_status_t st;
    if (fluval_ble_get_status(&st) && st.status_valid && !st.stale) {
        return true;
    }
    return s_has_status;
}

light_status_mode_t light_service_get_mode(void)
{
    fluval_status_t st;
    if (fluval_ble_get_status(&st) && st.status_valid && !st.stale) {
        if (st.mode == FLUVAL_MODE_AUTO) {
            return LIGHT_STATUS_AUTO;
        }
        if (st.mode == FLUVAL_MODE_MANUAL) {
            return LIGHT_STATUS_MANUAL;
        }
        return LIGHT_STATUS_UNKNOWN;
    }
    return s_mode;
}

uint8_t light_service_get_brightness_pct(void)
{
    fluval_status_t st;
    if (fluval_ble_get_status(&st) && st.status_valid && !st.stale) {
        return st.avg_output;
    }
    return s_brightness_pct;
}

bool light_service_is_light_online(void)
{
    fluval_status_t st;
    if (!fluval_ble_get_status(&st)) {
        return false;
    }
    return st.connected && st.subscribed && st.status_valid && !st.stale;
}
