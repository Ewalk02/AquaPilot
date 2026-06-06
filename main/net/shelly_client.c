#include "shelly_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "net/wifi_manager.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "shelly";

#define HTTP_TIMEOUT_MS   3500
#define RESPONSE_BUF_SIZE 768
#define HTTP_LOCK_MS      4000

static SemaphoreHandle_t s_http_lock;

static bool lock_http(void)
{
    if (s_http_lock == NULL) {
        s_http_lock = xSemaphoreCreateMutex();
        if (s_http_lock == NULL) {
            return false;
        }
    }

    return xSemaphoreTake(s_http_lock, pdMS_TO_TICKS(HTTP_LOCK_MS)) == pdTRUE;
}

static void unlock_http(void)
{
    if (s_http_lock != NULL) {
        xSemaphoreGive(s_http_lock);
    }
}

static bool host_for_plug(aquapilot_shelly_plug_t plug, char *host, size_t host_len)
{
    if (!aquapilot_wifi_is_connected()) {
        return false;
    }
    if (!aquapilot_settings_get_shelly_address(plug, host, host_len) || host[0] == '\0') {
        return false;
    }
    return true;
}

static esp_err_t http_read_response(esp_http_client_handle_t client, char *body, size_t body_len, int *out_status)
{
    if (body != NULL && body_len > 0) {
        body[0] = '\0';
    }

    (void)esp_http_client_fetch_headers(client);
    if (out_status != NULL) {
        *out_status = esp_http_client_get_status_code(client);
    }

    if (body != NULL && body_len > 1) {
        const int read = esp_http_client_read(client, body, (int)(body_len - 1));
        if (read < 0) {
            esp_http_client_close(client);
            return ESP_FAIL;
        }
        body[read] = '\0';
    }

    esp_http_client_close(client);
    return ESP_OK;
}

static esp_err_t http_get_body(const char *url, char *body, size_t body_len, int *out_status)
{
    if (!lock_http()) {
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        unlock_http();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        unlock_http();
        return err;
    }

    err = http_read_response(client, body, body_len, out_status);
    esp_http_client_cleanup(client);
    unlock_http();
    return err;
}

static esp_err_t http_post_json_body(const char *url, const char *payload, char *body, size_t body_len,
                                     int *out_status)
{
    if (!lock_http()) {
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        unlock_http();
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    const int payload_len = (int)strlen(payload);
    esp_http_client_set_post_field(client, payload, payload_len);

    esp_err_t err = esp_http_client_open(client, payload_len);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        unlock_http();
        return err;
    }

    err = http_read_response(client, body, body_len, out_status);
    esp_http_client_cleanup(client);
    unlock_http();
    return err;
}

static esp_err_t http_post_json(const char *url, const char *payload, int *out_status)
{
    return http_post_json_body(url, payload, NULL, 0, out_status);
}

static bool json_find_float(const char *json, const char *key, float *out)
{
    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }

    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }

    p += strlen(pattern);
    *out = strtof(p, NULL);
    return true;
}

static uint16_t watts_from_power(float power)
{
    if (power < 0.0f) {
        power = 0.0f;
    }
    return (uint16_t)(power + 0.5f);
}

static esp_err_t parse_apower_from_json(const char *json, uint16_t *watts)
{
    if (json == NULL || watts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *result = strstr(json, "\"result\"");
    const char *search = result != NULL ? result : json;

    float apower = 0.0f;
    if (!json_find_float(search, "apower", &apower)) {
        return ESP_ERR_NOT_FOUND;
    }

    *watts = watts_from_power(apower);
    return ESP_OK;
}

static esp_err_t shelly_gen2_get_power_watts(const char *host, uint16_t *watts)
{
    char url[192];
    char response[RESPONSE_BUF_SIZE];
    int status = 0;

    snprintf(url, sizeof(url), "http://%s/rpc/Switch.GetStatus?id=0", host);
    esp_err_t err = http_get_body(url, response, sizeof(response), &status);
    if (err == ESP_OK && status >= 200 && status < 300 && parse_apower_from_json(response, watts) == ESP_OK) {
        return ESP_OK;
    }

    snprintf(url, sizeof(url), "http://%s/rpc", host);
    static const char payload[] = "{\"id\":1,\"method\":\"Switch.GetStatus\",\"params\":{\"id\":0}}";
    err = http_post_json_body(url, payload, response, sizeof(response), &status);
    if (err == ESP_OK && status >= 200 && status < 300 && parse_apower_from_json(response, watts) == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "gen2 power read failed host=%s err=%s status=%d body=%.120s", host, esp_err_to_name(err), status,
             response);
    return err != ESP_OK ? err : ESP_FAIL;
}

static esp_err_t shelly_gen1_get_power_watts(const char *host, uint16_t *watts)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s/status", host);

    char body[RESPONSE_BUF_SIZE];
    int status = 0;
    esp_err_t err = http_get_body(url, body, sizeof(body), &status);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "gen1 status failed host=%s err=%s status=%d", host, esp_err_to_name(err), status);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    const char *meters = strstr(body, "\"meters\"");
    if (meters == NULL) {
        meters = body;
    }

    float power = 0.0f;
    if (!json_find_float(meters, "power", &power)) {
        ESP_LOGW(TAG, "gen1 power missing host=%s body=%.120s", host, body);
        return ESP_ERR_NOT_FOUND;
    }

    *watts = watts_from_power(power);
    return ESP_OK;
}

static esp_err_t shelly_gen1_relay_set(const char *host, bool on, int *out_status)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s/relay/0?turn=%s", host, on ? "on" : "off");
    return http_get_body(url, NULL, 0, out_status);
}

static esp_err_t shelly_gen2_switch_set(const char *host, bool on, int *out_status)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s/rpc", host);

    char payload[96];
    snprintf(payload, sizeof(payload), "{\"id\":1,\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":%s}}",
             on ? "true" : "false");
    return http_post_json(url, payload, out_status);
}

esp_err_t shelly_client_plug_set(aquapilot_shelly_plug_t plug, bool on)
{
    char host[AQUAPILOT_SHELLY_ADDR_MAX];
    if (!host_for_plug(plug, host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    int status = 0;
    esp_err_t err = shelly_gen2_switch_set(host, on, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "plug %d %s (gen2) host=%s status=%d", (int)plug, on ? "on" : "off", host, status);
        return ESP_OK;
    }

    err = shelly_gen1_relay_set(host, on, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "plug %d %s (gen1) host=%s status=%d", (int)plug, on ? "on" : "off", host, status);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "plug %d %s failed host=%s err=%s status=%d", (int)plug, on ? "on" : "off", host,
             esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
}

esp_err_t shelly_client_get_plug_power_watts(aquapilot_shelly_plug_t plug, uint16_t *watts)
{
    if (watts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char host[AQUAPILOT_SHELLY_ADDR_MAX];
    if (!host_for_plug(plug, host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = shelly_gen2_get_power_watts(host, watts);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    err = shelly_gen1_get_power_watts(host, watts);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    return err;
}

esp_err_t shelly_client_heater_plug_off(void)
{
    return shelly_client_plug_set(AQUAPILOT_SHELLY_HEATER, false);
}
