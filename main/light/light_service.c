#include "light_service.h"

#include "ble/ble_central_manager.h"
#include "chihiros_ble.h"
#include "fluval_ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater/heater_service.h"
#include "light_console.h"

#ifndef CONFIG_AQUAPILOT_LIGHT_BLE_NAME_PREFIX
#define CONFIG_AQUAPILOT_LIGHT_BLE_NAME_PREFIX "Plant4.0"
#endif

static const char *TAG = "light_svc";

#define BOOT_POLL_DELAY_US (30LL * 1000 * 1000)

static bool s_stale_recovery_requested;

static bool status_is_fresh(const fluval_status_t *st)
{
    return st != NULL && st->status_valid && !st->stale;
}

static void log_fresh_status(const fluval_status_t *st)
{
    if (!status_is_fresh(st)) {
        return;
    }

    ESP_LOGI(TAG, "light output %u%% (%s)", (unsigned)st->avg_output,
             st->mode == FLUVAL_MODE_AUTO ? "auto" : "manual");
}

static void refresh_from_ble_status(void)
{
    fluval_status_t st;
    if (fluval_ble_get_status(&st)) {
        log_fresh_status(&st);
    }
}

void light_service_request_poll(void)
{
    if (chihiros_ble_is_session_active() || fluval_ble_is_poll_window_active()) {
        return;
    }
    ESP_LOGI(TAG, "requesting light poll window");
    fluval_ble_request_poll_window();
}

void light_service_tick(void)
{
    fluval_status_t st;
    if (fluval_ble_get_status(&st) && st.status_valid && st.stale) {
        if (!s_stale_recovery_requested) {
            light_service_request_poll();
            s_stale_recovery_requested = true;
        }
    } else {
        s_stale_recovery_requested = false;
    }

    refresh_from_ble_status();
}

static void boot_poll_cb(void *arg)
{
    (void)arg;

    if (!light_service_status_is_known()) {
        ESP_LOGI(TAG, "boot light poll");
        light_service_request_poll();
    }
}

static void light_ble_tick_task(void *arg)
{
    (void)arg;

    while (true) {
        ble_central_manager_tick();
        light_service_tick();
        vTaskDelay(pdMS_TO_TICKS(2000));
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

    const esp_timer_create_args_t boot_poll_args = {
        .callback = boot_poll_cb,
        .name = "light_boot_poll",
    };
    esp_timer_handle_t boot_poll_timer;
    if (esp_timer_create(&boot_poll_args, &boot_poll_timer) == ESP_OK) {
        esp_timer_start_once(boot_poll_timer, BOOT_POLL_DELAY_US);
    }

    ESP_LOGI(TAG, "light service ready");
    return ESP_OK;
}

bool light_service_is_on(void)
{
    fluval_status_t st;
    if (!fluval_ble_get_status(&st) || !status_is_fresh(&st)) {
        return false;
    }
    return st.avg_output > 0;
}

bool light_service_status_is_known(void)
{
    fluval_status_t st;
    return fluval_ble_get_status(&st) && status_is_fresh(&st);
}

bool light_service_has_status(void)
{
    return light_service_status_is_known();
}

light_status_mode_t light_service_get_mode(void)
{
    fluval_status_t st;
    if (!fluval_ble_get_status(&st) || !status_is_fresh(&st)) {
        return LIGHT_STATUS_UNKNOWN;
    }

    if (st.mode == FLUVAL_MODE_AUTO) {
        return LIGHT_STATUS_AUTO;
    }
    if (st.mode == FLUVAL_MODE_MANUAL) {
        return LIGHT_STATUS_MANUAL;
    }
    return LIGHT_STATUS_UNKNOWN;
}

uint8_t light_service_get_brightness_pct(void)
{
    fluval_status_t st;
    if (!fluval_ble_get_status(&st) || !status_is_fresh(&st)) {
        return 0;
    }
    return st.avg_output;
}

bool light_service_is_light_online(void)
{
    fluval_status_t st;
    if (!fluval_ble_get_status(&st)) {
        return false;
    }
    return status_is_fresh(&st);
}
