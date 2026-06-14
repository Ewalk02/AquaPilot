#include "feeder_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/lan_http.h"
#include "net/wifi_manager.h"
#include "feeder_amount.h"
#include "safety/filter_calibration.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "feeder_client";

#define HTTP_TIMEOUT_MS          5000
#define MONITOR_INTERVAL_MS      5000
#define SYNC_INTERVAL_MS         60000
#define POLL_PHASE_MS            4200
#define HTTP_LOCK_MS             12000
#define OFFLINE_MISS_THRESHOLD   6
#define ONLINE_DISPLAY_GRACE_MS  90000

static volatile bool s_online;
static volatile bool s_remote_feeding;
static volatile bool s_push_requested;
static uint8_t s_miss_streak;
static int64_t s_last_success_ms;

static void feeder_mark_online(void)
{
    s_online = true;
    s_miss_streak = 0;
    s_last_success_ms = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void feeder_mark_offline_attempt(void)
{
    if (s_miss_streak < OFFLINE_MISS_THRESHOLD) {
        s_miss_streak++;
    }
    if (s_miss_streak >= OFFLINE_MISS_THRESHOLD) {
        s_online = false;
    }
}

static void feeder_mark_host_unavailable(void)
{
    s_online = false;
    s_miss_streak = OFFLINE_MISS_THRESHOLD;
    s_remote_feeding = false;
}

static bool json_has_true(const char *json, const char *key)
{
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\":true", key);
    return strstr(json, pattern) != NULL;
}

static bool json_read_u32(const char *json, const char *key, uint32_t *out)
{
    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }

    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }

    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    char *end = NULL;
    const unsigned long value = strtoul(p, &end, 10);
    if (end == p) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static esp_err_t http_request(const char *url, esp_http_client_method_t method, const char *post_body, int *out_status)
{
    if (!lan_http_wait_until_ready(pdMS_TO_TICKS(8000))) {
        return ESP_ERR_TIMEOUT;
    }
    lan_http_pace(false);
    if (!lan_http_acquire(pdMS_TO_TICKS(HTTP_LOCK_MS))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        lan_http_release();
        return ESP_ERR_NO_MEM;
    }

    if (method == HTTP_METHOD_POST && post_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_body, (int)strlen(post_body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (out_status != NULL) {
        *out_status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    lan_http_release();
    return err;
}

static esp_err_t http_get_body(const char *url, char *buf, size_t buf_len, int *out_status)
{
    if (buf == NULL || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    buf[0] = '\0';

    if (!lan_http_wait_until_ready(pdMS_TO_TICKS(8000))) {
        return ESP_ERR_TIMEOUT;
    }
    lan_http_pace(false);
    if (!lan_http_acquire(pdMS_TO_TICKS(HTTP_LOCK_MS))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        lan_http_release();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        const int len = esp_http_client_read(client, buf, (int)buf_len - 1);
        if (len > 0) {
            buf[len] = '\0';
        }
        if (out_status != NULL) {
            *out_status = esp_http_client_get_status_code(client);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    lan_http_release();
    return err;
}

static bool host_has_address(char *host, size_t host_len)
{
    if (!aquapilot_settings_get_feeder_host(host, host_len) || host[0] == '\0') {
        return false;
    }
    return true;
}

static bool host_configured(char *host, size_t host_len)
{
    if (!aquapilot_wifi_is_connected()) {
        return false;
    }
    return host_has_address(host, host_len);
}

static void refresh_online(void)
{
    if (filter_calibration_is_active()) {
        return;
    }

    if (lan_http_should_defer()) {
        return;
    }

    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_has_address(host, sizeof(host))) {
        feeder_mark_host_unavailable();
        return;
    }

    if (!aquapilot_wifi_is_connected()) {
        s_remote_feeding = false;
        return;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/status", host);

    char json[512];
    int status = 0;
    const esp_err_t err = http_get_body(url, json, sizeof(json), &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        feeder_mark_online();
        s_remote_feeding = json_has_true(json, "feeding");
    } else {
        feeder_mark_offline_attempt();
        s_remote_feeding = false;
    }
}

static bool get_own_ip(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return false;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        return false;
    }

    snprintf(buf, buf_len, IPSTR, IP2STR(&ip.ip));
    return true;
}

static esp_err_t push_schedule_internal(void)
{
    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_configured(host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    bool enabled = false;
    aquapilot_settings_get_feeder_enabled(&enabled);

    uint8_t start_h = 0;
    uint8_t start_m = 0;
    uint8_t end_h = 0;
    uint8_t end_m = 0;
    uint8_t times_per_day = 0;
    uint16_t amount_tenths = 0;
    if (!aquapilot_settings_get_feeder_schedule(&start_h, &start_m, &end_h, &end_m, &times_per_day,
                                                &amount_tenths)) {
        return ESP_FAIL;
    }

    char timezone[AQUAPILOT_TIMEZONE_MAX];
    if (!aquapilot_settings_get_timezone(timezone, sizeof(timezone)) || timezone[0] == '\0') {
        strncpy(timezone, "UTC0", sizeof(timezone) - 1);
    }

    char aquapilot_host[20] = "";
    get_own_ip(aquapilot_host, sizeof(aquapilot_host));

    char payload[320];
    snprintf(payload, sizeof(payload),
             "{\"enabled\":%s,\"start_h\":%u,\"start_m\":%u,\"end_h\":%u,\"end_m\":%u,"
             "\"times_per_day\":%u,\"amount_seconds\":%.1f,\"timezone\":\"%s\",\"aquapilot_host\":\"%s\"}",
             enabled ? "true" : "false", (unsigned)start_h, (unsigned)start_m, (unsigned)end_h, (unsigned)end_m,
             (unsigned)times_per_day, (double)feeder_amount_tenths_to_seconds(amount_tenths), timezone,
             aquapilot_host);

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/schedule", host);

    int status = 0;
    const esp_err_t err = http_request(url, HTTP_METHOD_POST, payload, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        feeder_mark_online();
        ESP_LOGI(TAG, "schedule pushed to %s", host);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "schedule push failed host=%s err=%s status=%d", host, esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == IP_EVENT_STA_GOT_IP) {
        s_push_requested = true;
    }
}

static void monitor_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(POLL_PHASE_MS));

    int64_t last_sync_ms = 0;

    while (true) {
        refresh_online();

        const int64_t now_ms = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (s_push_requested || (now_ms - last_sync_ms) >= SYNC_INTERVAL_MS) {
            s_push_requested = false;
            if (aquapilot_settings_has_feeder_host()) {
                if (push_schedule_internal() == ESP_OK) {
                    last_sync_ms = now_ms;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t feeder_client_init(void)
{
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);

    if (xTaskCreate(monitor_task, "feeder_client", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "feeder client started");
    return ESP_OK;
}

bool feeder_client_is_online(void)
{
    if (s_last_success_ms > 0) {
        const int64_t now_ms = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((now_ms - s_last_success_ms) < ONLINE_DISPLAY_GRACE_MS) {
            return true;
        }
    }

    return s_online;
}

bool feeder_client_is_feeding(void)
{
    return s_remote_feeding;
}

esp_err_t feeder_client_feed(uint16_t amount_tenths)
{
    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_configured(host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/feed", host);

    char payload[48];
    snprintf(payload, sizeof(payload), "{\"seconds\":%.1f}", (double)feeder_amount_tenths_to_seconds(amount_tenths));

    int status = 0;
    const esp_err_t err = http_request(url, HTTP_METHOD_POST, payload, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        feeder_mark_online();
        ESP_LOGI(TAG, "feed command sent (%.1f s) host=%s", (double)feeder_amount_tenths_to_seconds(amount_tenths),
                 host);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "feed command failed host=%s err=%s status=%d", host, esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
}

esp_err_t feeder_client_push_schedule(void)
{
    return push_schedule_internal();
}

esp_err_t feeder_client_skip_next(void)
{
    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_configured(host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/skip", host);

    int status = 0;
    const esp_err_t err = http_request(url, HTTP_METHOD_POST, "{}", &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        feeder_mark_online();
        ESP_LOGI(TAG, "skip command sent host=%s", host);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "skip command failed host=%s err=%s status=%d", host, esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
}

bool feeder_client_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return false;
    }

    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_configured(host, sizeof(host))) {
        return false;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/status", host);

    int status = 0;
    const esp_err_t err = http_get_body(url, buf, len, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        feeder_mark_online();
        return true;
    }

    return false;
}

void feeder_client_request_schedule_push(void)
{
    s_push_requested = true;
}

bool feeder_client_get_feed_status(feeder_client_feed_status_t *out)
{
    if (out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    char json[512];
    if (!feeder_client_get_status_json(json, sizeof(json))) {
        return false;
    }

    out->valid = true;
    out->feeding = json_has_true(json, "feeding");

    uint32_t value = 0;
    if (json_read_u32(json, "feed_duration_ms", &value)) {
        out->feed_duration_ms = value;
        out->feed_tenths = (uint16_t)((value + 99U) / 100U);
    }
    if (json_read_u32(json, "feed_elapsed_ms", &value)) {
        out->feed_elapsed_ms = value;
    }
    if (json_read_u32(json, "feed_steps", &value)) {
        out->feed_steps = value;
    }

    return true;
}

void feeder_client_format_feed_status(const feeder_client_feed_status_t *status, char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (status == NULL || !status->valid) {
        snprintf(buf, len, "Feeding...");
        return;
    }

    const uint32_t duration_ms = status->feed_duration_ms > 0 ? status->feed_duration_ms
                                                              : feeder_amount_tenths_to_ms(status->feed_tenths);
    const uint32_t elapsed_s = status->feed_elapsed_ms / 1000U;
    const uint32_t duration_s = duration_ms / 1000U;
    const uint32_t duration_tenths = (duration_ms + 99U) / 100U;
    const uint32_t elapsed_tenths = (status->feed_elapsed_ms + 99U) / 100U;

    if (status->feeding) {
        if (duration_ms % 1000U == 0) {
            snprintf(buf, len, "Feeding %u/%us, %u steps", (unsigned)elapsed_s, (unsigned)duration_s,
                     (unsigned)status->feed_steps);
        } else {
            snprintf(buf, len, "Feeding %.1f/%.1fs, %u steps", (double)elapsed_tenths / 10.0,
                     (double)duration_tenths / 10.0, (unsigned)status->feed_steps);
        }
        return;
    }

    snprintf(buf, len, "Feed complete (%u steps)", (unsigned)status->feed_steps);
}
