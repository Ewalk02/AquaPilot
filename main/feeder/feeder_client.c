#include "feeder_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi_manager.h"
#include "safety/filter_calibration.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "feeder_client";

#define HTTP_TIMEOUT_MS     4000
#define MONITOR_INTERVAL_MS 5000
#define POLL_PHASE_MS       2800

static volatile bool s_online;

static esp_err_t http_request(const char *url, esp_http_client_method_t method, const char *post_body,
                              int *out_status)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
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
    return err;
}

static bool host_configured(char *host, size_t host_len)
{
    if (!aquapilot_wifi_is_connected()) {
        return false;
    }
    if (!aquapilot_settings_get_feeder_host(host, host_len) || host[0] == '\0') {
        return false;
    }
    return true;
}

static void refresh_online(void)
{
    if (filter_calibration_is_active()) {
        return;
    }

    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_configured(host, sizeof(host))) {
        s_online = false;
        return;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/status", host);

    int status = 0;
    const esp_err_t err = http_request(url, HTTP_METHOD_GET, NULL, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        s_online = true;
    } else {
        s_online = false;
    }
}

static void monitor_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(POLL_PHASE_MS));

    while (true) {
        refresh_online();
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t feeder_client_init(void)
{
    if (xTaskCreate(monitor_task, "feeder_client", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "feeder client started");
    return ESP_OK;
}

bool feeder_client_is_online(void)
{
    return s_online;
}

esp_err_t feeder_client_feed(uint16_t seconds)
{
    char host[AQUAPILOT_FEEDER_HOST_MAX];
    if (!host_configured(host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/feed", host);

    char payload[48];
    snprintf(payload, sizeof(payload), "{\"seconds\":%u}", (unsigned)seconds);

    int status = 0;
    const esp_err_t err = http_request(url, HTTP_METHOD_POST, payload, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        s_online = true;
        ESP_LOGI(TAG, "feed command sent (%u s) host=%s", (unsigned)seconds, host);
        return ESP_OK;
    }

    s_online = false;
    ESP_LOGW(TAG, "feed command failed host=%s err=%s status=%d", host, esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
}
