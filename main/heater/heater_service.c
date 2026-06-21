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

#define BLE_TICK_MS           2000
#define RETRY_INTERVAL_MS     15000
#define BURST_DURATION_MS     (5 * 60 * 1000)
#define COOLDOWN_MS           (30 * 60 * 1000)
#define POLL_INTERVAL_MS      (5 * 60 * 1000)
#define READING_STALE_MS      (10 * 60 * 1000)
#define WIFI_DEFER_TIMEOUT_MS 15000

typedef enum {
    HEATER_LINK_BURST = 0,
    HEATER_LINK_CONNECTED,
    HEATER_LINK_COOLDOWN,
} heater_link_state_t;

static float s_temp_f;
static uint16_t s_power_watts;
static bool s_heating;
static bool s_has_reading;
static bool s_connected;
static bool s_wifi_sta_requested;
static bool s_setpoint_session_pending;
static esp_timer_handle_t s_wifi_defer_timer;
static float s_applied_setpoint_f = -1.0f;
static bool s_ble_task_running;

static heater_link_state_t s_link_state;
static uint32_t s_burst_start_ms;
static uint32_t s_last_attempt_ms;
static uint32_t s_last_reading_ms;
static uint32_t s_cooldown_until_ms;
static uint32_t s_next_poll_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void store_reading(const chihiros_status_t *st)
{
    if (st == NULL || !st->status_valid) {
        return;
    }

    s_temp_f = st->current_temp_f;
    s_power_watts = st->power_watts;
    s_heating = st->heating;
    s_has_reading = true;
    s_last_reading_ms = now_ms();
    s_connected = true;
    s_link_state = HEATER_LINK_CONNECTED;
    s_next_poll_ms = s_last_reading_ms + POLL_INTERVAL_MS;
    ESP_LOGI(TAG, "heater connected: %.1f F (%u W%s)", s_temp_f, (unsigned)s_power_watts,
             s_heating ? ", heating" : "");
}

static void start_burst(const char *reason)
{
    s_link_state = HEATER_LINK_BURST;
    s_burst_start_ms = now_ms();
    s_last_attempt_ms = 0;
    s_connected = false;
    ESP_LOGI(TAG, "heater reconnect burst (%s): 15 s retries for 5 min", reason);
}

static void request_read_session(void)
{
    if (chihiros_ble_is_session_active() || ble_central_manager_is_light_exclusive()) {
        return;
    }
    s_last_attempt_ms = now_ms();
    chihiros_ble_request_session(CHIHIROS_BLE_SESSION_READ);
}

static void heater_link_tick(void)
{
    const uint32_t now = now_ms();

    if (s_link_state == HEATER_LINK_CONNECTED) {
        if (s_last_reading_ms != 0 && (now - s_last_reading_ms) > READING_STALE_MS) {
            ESP_LOGW(TAG, "heater reading stale (%lu min old), reconnecting",
                     (unsigned long)((now - s_last_reading_ms) / 60000U));
            start_burst("reading stale");
            return;
        }

        if (now >= s_next_poll_ms) {
            ESP_LOGI(TAG, "scheduled heater read (5 min)");
            s_next_poll_ms = now + POLL_INTERVAL_MS;
            request_read_session();
        }
        return;
    }

    if (s_link_state == HEATER_LINK_BURST) {
        if (now - s_burst_start_ms >= BURST_DURATION_MS) {
            ESP_LOGW(TAG, "heater reconnect burst ended without a reading");
            s_connected = false;
            s_link_state = HEATER_LINK_COOLDOWN;
            s_cooldown_until_ms = now + COOLDOWN_MS;
            ESP_LOGI(TAG, "heater disconnected; next burst in 30 min");
            return;
        }

        if (s_last_attempt_ms == 0 || (now - s_last_attempt_ms) >= RETRY_INTERVAL_MS) {
            request_read_session();
        }
        return;
    }

    if (s_link_state == HEATER_LINK_COOLDOWN && now >= s_cooldown_until_ms) {
        start_burst("cooldown elapsed");
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

static void on_heater_session_done(void)
{
    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st) || !st.status_valid) {
        ESP_LOGW(TAG, "heater read session finished without valid data");
        return;
    }

    store_reading(&st);
    if (!s_wifi_sta_requested) {
        request_wifi_sta_if_needed("heater status received");
    }
}

static void try_apply_saved_setpoint(void)
{
    if (!s_setpoint_session_pending) {
        return;
    }

    float setpoint_f = 0.0f;
    if (!aquapilot_settings_has_heater_setpoint() ||
        !aquapilot_settings_get_heater_setpoint(&setpoint_f)) {
        s_applied_setpoint_f = -1.0f;
        s_setpoint_session_pending = false;
        chihiros_ble_end_session();
        return;
    }

    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st) || !st.connected || !st.subscribed) {
        return;
    }

    if (s_applied_setpoint_f == setpoint_f) {
        s_setpoint_session_pending = false;
        chihiros_ble_end_session();
        return;
    }

    const esp_err_t err = chihiros_ble_set_target_f(setpoint_f);
    if (err == ESP_OK) {
        s_applied_setpoint_f = setpoint_f;
        s_setpoint_session_pending = false;
        ESP_LOGI(TAG, "heater setpoint %.1f F applied", setpoint_f);
        chihiros_ble_end_session();
    }
}

static void ble_tick_task(void *arg)
{
    (void)arg;

    while (true) {
        ble_central_manager_tick();
        heater_link_tick();

        if (chihiros_ble_is_session_active() && chihiros_ble_get_session_mode() == CHIHIROS_BLE_SESSION_SETPOINT) {
            try_apply_saved_setpoint();
        }

        vTaskDelay(pdMS_TO_TICKS(BLE_TICK_MS));
    }
}

void heater_service_request_setpoint_apply(void)
{
    s_applied_setpoint_f = -1.0f;
    s_setpoint_session_pending = true;
    chihiros_ble_request_session(CHIHIROS_BLE_SESSION_SETPOINT);
}

esp_err_t heater_service_init(void)
{
    chihiros_ble_set_name_prefix(CONFIG_AQUAPILOT_HEATER_BLE_NAME_PREFIX);
    chihiros_ble_set_session_done_cb(on_heater_session_done);

    esp_err_t err = chihiros_ble_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chihiros BLE start failed: %s", esp_err_to_name(err));
        return err;
    }

    heater_console_register();

    s_has_reading = false;
    s_connected = false;
    start_burst("boot");

    if (xTaskCreate(ble_tick_task, "ble_tick", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create ble_tick task");
        return ESP_FAIL;
    }
    s_ble_task_running = true;

    const esp_timer_create_args_t wifi_defer_args = {
        .callback = wifi_defer_timeout_cb,
        .name = "wifi_defer",
    };
    err = esp_timer_create(&wifi_defer_args, &s_wifi_defer_timer);
    if (err == ESP_OK) {
        esp_timer_start_once(s_wifi_defer_timer, (uint64_t)WIFI_DEFER_TIMEOUT_MS * 1000ULL);
    }

    ESP_LOGI(TAG, "heater service started (prefix=%s)", CONFIG_AQUAPILOT_HEATER_BLE_NAME_PREFIX);
    return ESP_OK;
}

bool heater_service_ble_task_is_running(void)
{
    return s_ble_task_running;
}

bool heater_service_get_temp_f(float *out_temp_f)
{
    if (!s_connected || !s_has_reading || out_temp_f == NULL) {
        return false;
    }
    *out_temp_f = s_temp_f;
    return true;
}

bool heater_service_get_power_watts(uint16_t *out_watts)
{
    if (!s_connected || !s_has_reading || out_watts == NULL) {
        return false;
    }
    *out_watts = s_power_watts;
    return true;
}

bool heater_service_has_reading(void)
{
    return s_connected && s_has_reading;
}

bool heater_service_is_heater_online(void)
{
    return s_connected;
}

bool heater_service_is_heater_off(void)
{
    if (!s_connected || !s_has_reading) {
        return false;
    }
    return !s_heating;
}

const char *heater_service_source_text(void)
{
    if (chihiros_ble_is_session_active()) {
        return "Reading heater...";
    }
    if (s_connected) {
        return "Chihiros heater";
    }
    if (s_link_state == HEATER_LINK_BURST) {
        return "Connecting to heater...";
    }
    return "Heater disconnected";
}
