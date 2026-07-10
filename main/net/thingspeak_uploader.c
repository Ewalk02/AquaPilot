#include "thingspeak_uploader.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater/heater_service.h"
#include "net/wifi_manager.h"
#include "safety/air_power_monitor.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_power_monitor.h"
#include "schedule/aquapilot_time.h"
#include "schedule/feeder_service.h"
#include "storage/aquapilot_settings.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "thingspeak";

#define TASK_STACK_BYTES          12288
#define TASK_PRIORITY             3
#define TASK_INTERVAL_MS          60000
#define MIN_REQUEST_GAP_US        (15LL * 1000000LL)
#define URL_BUF_SIZE              512
#define STATUS_BUF_SIZE           128
#define FAILURE_REASON_SIZE       48
#define STALE_INTERVAL_MULTIPLIER 2
#define MAX_CONSECUTIVE_FAILURES  5
#define STACK_LOG_EVERY_N_UPLOADS 24

static char s_status[STATUS_BUF_SIZE] = "ThingSpeak Disabled";
static char s_last_failure_reason[FAILURE_REASON_SIZE] = "";
static int64_t s_last_request_us;
static int64_t s_field_last_upload_us[AQUAPILOT_THINGSPEAK_FIELD_COUNT];
static int64_t s_last_success_us;
static bool s_connected;
static bool s_upload_in_progress;
static bool s_force_all_due;
static uint8_t s_consecutive_failures;
static uint32_t s_upload_count;
static TaskHandle_t s_uploader_task_handle;

static void set_log_status(const char *text)
{
    if (text == NULL) {
        return;
    }
    snprintf(s_status, sizeof(s_status), "%s", text);
}

static void set_failure_reason(const char *text)
{
    if (text == NULL) {
        s_last_failure_reason[0] = '\0';
        return;
    }
    snprintf(s_last_failure_reason, sizeof(s_last_failure_reason), "%s", text);
}

static uint16_t max_configured_interval_min(void)
{
    uint16_t max_interval = 0;

    for (int i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        aquapilot_ts_metric_t metric = AQUAPILOT_TS_METRIC_NONE;
        uint16_t interval_min = 0;
        if (!aquapilot_settings_get_thingspeak_field((uint8_t)i, &metric, &interval_min)) {
            continue;
        }
        if (metric == AQUAPILOT_TS_METRIC_NONE || interval_min == 0) {
            continue;
        }
        if (interval_min > max_interval) {
            max_interval = interval_min;
        }
    }

    return max_interval > 0 ? max_interval : 30;
}

static int64_t stale_threshold_us(void)
{
    const uint16_t max_interval = max_configured_interval_min();
    return (int64_t)max_interval * STALE_INTERVAL_MULTIPLIER * 60LL * 1000000LL;
}

static bool field_is_due(int64_t now_us, int64_t last_us, uint16_t interval_min)
{
    if (last_us == 0) {
        return true;
    }

    const int64_t interval_us = (int64_t)interval_min * 60LL * 1000000LL;
    return (now_us - last_us) >= interval_us;
}

static void clear_upload_timestamps(void)
{
    memset(s_field_last_upload_us, 0, sizeof(s_field_last_upload_us));
    s_force_all_due = true;
}

static void trigger_network_recovery(void)
{
    ESP_LOGW(TAG, "network recovery after %u consecutive upload failures", (unsigned)s_consecutive_failures);

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_dhcpc_stop(netif);
        esp_netif_dhcpc_start(netif);
    }

    esp_wifi_disconnect();
    esp_wifi_connect();
}

static void log_stack_watermark(void)
{
    if (s_uploader_task_handle == NULL) {
        return;
    }

    const UBaseType_t watermark = uxTaskGetStackHighWaterMark(s_uploader_task_handle);
    ESP_LOGI(TAG, "stack high watermark: %u words free", (unsigned)watermark);
}

static void record_upload_success(void)
{
    const int64_t now_us = esp_timer_get_time();

    s_connected = true;
    s_consecutive_failures = 0;
    set_failure_reason("");
    s_last_success_us = now_us;
    s_upload_count++;

    if (s_upload_count % STACK_LOG_EVERY_N_UPLOADS == 0) {
        log_stack_watermark();
    }
}

static void record_upload_failure(const char *reason)
{
    s_connected = false;
    set_failure_reason(reason);
    s_consecutive_failures++;

    if (s_consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
        trigger_network_recovery();
        s_consecutive_failures = 0;
    }
}

void thingspeak_uploader_on_clock_step(void)
{
    ESP_LOGW(TAG, "clock stepped backward, forcing ThingSpeak re-upload");
    clear_upload_timestamps();
}

const char *thingspeak_uploader_last_status_text(void)
{
    static char buf[STATUS_BUF_SIZE];

    bool enabled = false;
    if (!aquapilot_settings_get_thingspeak_enabled(&enabled) || !enabled) {
        s_connected = false;
        return "ThingSpeak Disabled";
    }
    if (s_upload_in_progress) {
        return "ThingSpeak Connecting...";
    }
    if (!aquapilot_wifi_is_connected()) {
        return "Waiting for Wi-Fi";
    }
    if (!aquapilot_time_is_ready()) {
        return "Waiting for time";
    }

    if (s_last_success_us > 0) {
        const int64_t age_us = esp_timer_get_time() - s_last_success_us;
        const int64_t age_min = age_us / (60LL * 1000000LL);
        const int64_t stale_min = stale_threshold_us() / (60LL * 1000000LL);

        if (age_min >= stale_min) {
            if (s_consecutive_failures > 0) {
                snprintf(buf, sizeof(buf), "Stale - no upload %lldm (%u failures)", (long long)age_min,
                         (unsigned)s_consecutive_failures);
            } else if (s_last_failure_reason[0] != '\0') {
                snprintf(buf, sizeof(buf), "Stale - no upload %lldm (%s)", (long long)age_min, s_last_failure_reason);
            } else {
                snprintf(buf, sizeof(buf), "Stale - no upload %lldm", (long long)age_min);
            }
            return buf;
        }

        if (age_min <= 0) {
            snprintf(buf, sizeof(buf), "Connected - last upload just now");
        } else {
            snprintf(buf, sizeof(buf), "Connected - last upload %lldm ago", (long long)age_min);
        }
        return buf;
    }

    if (s_consecutive_failures > 0 && s_last_failure_reason[0] != '\0') {
        snprintf(buf, sizeof(buf), "Connecting... (%s)", s_last_failure_reason);
        return buf;
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
    case AQUAPILOT_TS_METRIC_AIR_W: {
        uint16_t watts = 0;
        if (!air_power_monitor_get_watts(&watts)) {
            return false;
        }
        *out_value = (double)watts;
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
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        s_upload_in_progress = false;
        record_upload_failure("HTTP init failed");
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
        record_upload_failure(esp_err_to_name(err));
        ESP_LOGW(TAG, "upload failed: %s", esp_err_to_name(err));
        set_log_status("upload failed");
        return false;
    }
    if (status != 200) {
        record_upload_failure("HTTP error");
        ESP_LOGW(TAG, "upload HTTP %d", status);
        set_log_status("HTTP error");
        return false;
    }

    long entry_id = strtol(response, NULL, 10);
    if (entry_id <= 0) {
        record_upload_failure("update rejected");
        ESP_LOGW(TAG, "upload rejected: %s", response);
        set_log_status("update rejected");
        return false;
    }

    record_upload_success();
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

    const int64_t now_us = esp_timer_get_time();
    if (s_last_request_us != 0 && (now_us - s_last_request_us) < MIN_REQUEST_GAP_US) {
        return false;
    }

    if (!upload_url(url)) {
        return false;
    }

    s_last_request_us = now_us;

    for (int i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        if (uploaded_fields[i]) {
            s_field_last_upload_us[i] = now_us;
        }
    }
    return true;
}

static void check_stale_watchdog(int64_t now_us)
{
    if (s_last_success_us == 0) {
        return;
    }

    if ((now_us - s_last_success_us) < stale_threshold_us()) {
        return;
    }

    ESP_LOGW(TAG, "stale upload watchdog firing");
    clear_upload_timestamps();
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

        const int64_t now_us = esp_timer_get_time();
        check_stale_watchdog(now_us);

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

            if (s_force_all_due || field_is_due(now_us, s_field_last_upload_us[i], interval_min)) {
                due_fields[i] = true;
                any_due = true;
            }
        }

        s_force_all_due = false;

        if (!any_due) {
            continue;
        }

        (void)perform_upload(due_fields);
    }
}

esp_err_t thingspeak_uploader_init(void)
{
    memset(s_field_last_upload_us, 0, sizeof(s_field_last_upload_us));
    s_last_request_us = 0;
    s_last_success_us = 0;
    s_connected = false;
    s_upload_in_progress = false;
    s_force_all_due = false;
    s_consecutive_failures = 0;
    s_upload_count = 0;
    s_uploader_task_handle = NULL;
    set_failure_reason("");
    set_log_status("started");

    BaseType_t ok = xTaskCreate(uploader_task, "thingspeak", TASK_STACK_BYTES / sizeof(StackType_t), NULL,
                                TASK_PRIORITY, &s_uploader_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to start uploader task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "uploader started");
    return ESP_OK;
}
