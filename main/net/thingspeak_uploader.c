#include "thingspeak_uploader.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater/heater_service.h"
#include "net/wifi_manager.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_power_monitor.h"
#include "schedule/aquapilot_time.h"
#include "schedule/feeder_service.h"
#include "storage/aquapilot_settings.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "thingspeak";

#define TASK_STACK_BYTES     8192
#define TASK_PRIORITY        3
#define TASK_INTERVAL_MS     60000
#define MIN_REQUEST_GAP_S    15
#define URL_BUF_SIZE         512
#define STATUS_BUF_SIZE      96

static char s_status[STATUS_BUF_SIZE] = "ThingSpeak Disabled";
static time_t s_last_request_epoch;
static time_t s_field_last_upload[AQUAPILOT_THINGSPEAK_FIELD_COUNT];
static bool s_connected;
static bool s_upload_in_progress;

static void set_log_status(const char *text)
{
    if (text == NULL) {
        return;
    }
    snprintf(s_status, sizeof(s_status), "%s", text);
}

const char *thingspeak_uploader_last_status_text(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_thingspeak_enabled(&enabled) || !enabled) {
        s_connected = false;
        return "ThingSpeak Disabled";
    }
    if (s_upload_in_progress) {
        return "ThingSpeak Connecting...";
    }
    if (s_connected) {
        return "ThingSpeak Connected";
    }
    return "ThingSpeak Connecting...";
}

static bool metric_value_ready(aquapilot_ts_metric_t metric, double *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    switch (metric) {
    case AQUAPILOT_TS_METRIC_TEMP_F: {
        float temp_f = 0.0f;
        if (!heater_service_is_heater_online() || !heater_service_get_temp_f(&temp_f) || temp_f <= 0.0f) {
            return false;
        }
        *out_value = (double)temp_f;
        return true;
    }
    case AQUAPILOT_TS_METRIC_FILTER_W: {
        uint16_t watts = 0;
        if (!filter_power_monitor_get_watts(&watts)) {
            return false;
        }
        *out_value = (double)watts;
        return true;
    }
    case AQUAPILOT_TS_METRIC_CO2_W: {
        uint16_t watts = 0;
        if (!co2_power_monitor_get_watts(&watts)) {
            return false;
        }
        *out_value = (double)watts;
        return true;
    }
    case AQUAPILOT_TS_METRIC_FEED_STATUS: {
        bool success = false;
        bool valid = false;
        if (!feeder_service_get_last_feed_outcome(&success, &valid) || !valid) {
            return false;
        }
        *out_value = success ? 1.0 : 0.0;
        return true;
    }
    default:
        return false;
    }
}

static bool upload_url(const char *url)
{
    s_upload_in_progress = true;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        s_upload_in_progress = false;
        s_connected = false;
        set_log_status("HTTP init failed");
        return false;
    }

    char response[32] = {0};
    esp_err_t err = esp_http_client_open(client, 0);
    int status = 0;
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        const int len = esp_http_client_read(client, response, (int)sizeof(response) - 1);
        if (len > 0) {
            response[len] = '\0';
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    s_upload_in_progress = false;

    if (err != ESP_OK) {
        s_connected = false;
        ESP_LOGW(TAG, "upload failed: %s", esp_err_to_name(err));
        set_log_status("upload failed");
        return false;
    }
    if (status != 200) {
        s_connected = false;
        ESP_LOGW(TAG, "upload HTTP %d", status);
        set_log_status("HTTP error");
        return false;
    }

    long entry_id = strtol(response, NULL, 10);
    if (entry_id <= 0) {
        s_connected = false;
        ESP_LOGW(TAG, "upload rejected: %s", response);
        set_log_status("update rejected");
        return false;
    }

    s_connected = true;
    ESP_LOGI(TAG, "upload ok entry %ld", entry_id);
    set_log_status("upload ok");
    return true;
}

static bool perform_upload(const bool due_fields[AQUAPILOT_THINGSPEAK_FIELD_COUNT])
{
    char api_key[AQUAPILOT_THINGSPEAK_KEY_MAX + 1];
    if (!aquapilot_settings_get_thingspeak_api_key(api_key, sizeof(api_key)) || api_key[0] == '\0') {
        set_log_status("API key missing");
        return false;
    }

    char url[URL_BUF_SIZE];
    int used = snprintf(url, sizeof(url), "https://api.thingspeak.com/update?api_key=%s", api_key);
    if (used <= 0 || used >= (int)sizeof(url)) {
        set_log_status("URL too long");
        return false;
    }

    bool uploaded_fields[AQUAPILOT_THINGSPEAK_FIELD_COUNT] = {0};
    bool any_field = false;
    for (int i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        if (!due_fields[i]) {
            continue;
        }

        aquapilot_ts_metric_t metric = AQUAPILOT_TS_METRIC_NONE;
        uint16_t interval_min = 0;
        if (!aquapilot_settings_get_thingspeak_field((uint8_t)i, &metric, &interval_min)) {
            continue;
        }
        if (metric == AQUAPILOT_TS_METRIC_NONE || interval_min == 0) {
            continue;
        }

        double value = 0.0;
        if (!metric_value_ready(metric, &value)) {
            ESP_LOGW(TAG, "field %d metric %u not ready", i + 1, (unsigned)metric);
            continue;
        }

        const int written = snprintf(url + used, sizeof(url) - (size_t)used, "&field%d=%.3g", i + 1, value);
        if (written <= 0 || used + written >= (int)sizeof(url)) {
            set_log_status("URL too long");
            return false;
        }
        used += written;
        uploaded_fields[i] = true;
        any_field = true;
    }

    if (!any_field) {
        set_log_status("waiting for data");
        return false;
    }

    const time_t now = time(NULL);
    if (s_last_request_epoch != 0 && now - s_last_request_epoch < MIN_REQUEST_GAP_S) {
        return false;
    }

    if (!upload_url(url)) {
        return false;
    }

    s_last_request_epoch = now;

    for (int i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        if (uploaded_fields[i]) {
            s_field_last_upload[i] = now;
        }
    }
    return true;
}

static void uploader_task(void *arg)
{
    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TASK_INTERVAL_MS));

        bool enabled = false;
        if (!aquapilot_settings_get_thingspeak_enabled(&enabled) || !enabled) {
            s_connected = false;
            set_log_status("disabled");
            continue;
        }
        if (!aquapilot_wifi_is_connected()) {
            set_log_status("waiting for Wi-Fi");
            continue;
        }
        if (!aquapilot_time_is_ready()) {
            set_log_status("waiting for time");
            continue;
        }

        const time_t now = time(NULL);
        bool due_fields[AQUAPILOT_THINGSPEAK_FIELD_COUNT] = {0};
        bool any_due = false;

        for (int i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
            aquapilot_ts_metric_t metric = AQUAPILOT_TS_METRIC_NONE;
            uint16_t interval_min = 0;
            if (!aquapilot_settings_get_thingspeak_field((uint8_t)i, &metric, &interval_min)) {
                continue;
            }
            if (metric == AQUAPILOT_TS_METRIC_NONE || interval_min == 0) {
                continue;
            }

            const time_t interval_s = (time_t)interval_min * 60;
            if (s_field_last_upload[i] == 0 || now - s_field_last_upload[i] >= interval_s) {
                due_fields[i] = true;
                any_due = true;
            }
        }

        if (!any_due) {
            continue;
        }

        (void)perform_upload(due_fields);
    }
}

esp_err_t thingspeak_uploader_init(void)
{
    memset(s_field_last_upload, 0, sizeof(s_field_last_upload));
    s_last_request_epoch = 0;
    s_connected = false;
    s_upload_in_progress = false;
    set_log_status("started");

    BaseType_t ok = xTaskCreate(uploader_task, "thingspeak", TASK_STACK_BYTES / sizeof(StackType_t), NULL,
                                TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to start uploader task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "uploader started");
    return ESP_OK;
}
