#include "shelly_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/lan_http.h"
#include "net/wifi_manager.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "shelly";

#define HTTP_TIMEOUT_MS       5000
#define HTTP_POWER_TIMEOUT_MS 8000
#define RESPONSE_BUF_SIZE     768
#define HTTP_LOCK_MS          12000
#define API_CACHE_SLOTS       3
#define DEFER_WAIT_MS         8000

typedef enum {
    SHELLY_API_UNKNOWN = 0,
    SHELLY_API_RPC,
} shelly_api_kind_t;

typedef struct {
    char host[AQUAPILOT_SHELLY_ADDR_MAX];
    shelly_api_kind_t kind;
} shelly_api_cache_t;

static shelly_api_cache_t s_api_cache[API_CACHE_SLOTS];
static bool s_exclusive_filter;

static shelly_api_kind_t api_cache_lookup(const char *host)
{
    for (size_t i = 0; i < API_CACHE_SLOTS; i++) {
        if (s_api_cache[i].host[0] != '\0' && strcmp(s_api_cache[i].host, host) == 0) {
            return s_api_cache[i].kind;
        }
    }
    return SHELLY_API_UNKNOWN;
}

static void api_cache_store(const char *host, shelly_api_kind_t kind)
{
    size_t slot = 0;
    for (size_t i = 0; i < API_CACHE_SLOTS; i++) {
        if (s_api_cache[i].host[0] != '\0' && strcmp(s_api_cache[i].host, host) == 0) {
            slot = i;
            break;
        }
        if (s_api_cache[i].host[0] == '\0') {
            slot = i;
            break;
        }
    }

    snprintf(s_api_cache[slot].host, sizeof(s_api_cache[slot].host), "%s", host);
    s_api_cache[slot].kind = kind;
}

static bool prepare_http_transaction(void)
{
    if (!s_exclusive_filter) {
        (void)lan_http_wait_until_ready(pdMS_TO_TICKS(DEFER_WAIT_MS));
    }
    lan_http_pace(s_exclusive_filter);
    return lan_http_acquire(pdMS_TO_TICKS(HTTP_LOCK_MS));
}

static void finish_http_transaction(void)
{
    lan_http_release();
}

static char s_auth_user[] = "admin";
static char s_auth_pass[AQUAPILOT_SHELLY_PASSWORD_MAX + 1];

static void apply_shelly_auth(esp_http_client_config_t *config)
{
    s_auth_pass[0] = '\0';
    if (!aquapilot_settings_get_shelly_password(s_auth_pass, sizeof(s_auth_pass)) || s_auth_pass[0] == '\0') {
        return;
    }

    config->username = s_auth_user;
    config->password = s_auth_pass;
    config->auth_type = HTTP_AUTH_TYPE_DIGEST;
    config->buffer_size_tx = 1024;
}

static void log_http_status_issue(const char *host, const char *op, int status)
{
    if (status == 401) {
        ESP_LOGW(TAG, "%s host=%s: auth required (set Shelly password in Settings)", op, host);
    }
}

typedef struct {
    char *body;
    size_t body_cap;
    size_t body_len;
} shelly_http_body_t;

static esp_err_t shelly_http_event_handler(esp_http_client_event_t *evt)
{
    shelly_http_body_t *acc = (shelly_http_body_t *)evt->user_data;
    if (acc == NULL || acc->body == NULL || acc->body_cap <= 1) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        if (esp_http_client_is_chunked_response(evt->client)) {
            return ESP_OK;
        }
        const size_t room = acc->body_cap - 1 - acc->body_len;
        const size_t copy = evt->data_len < room ? (size_t)evt->data_len : room;
        if (copy > 0) {
            memcpy(acc->body + acc->body_len, evt->data, copy);
            acc->body_len += copy;
            acc->body[acc->body_len] = '\0';
        }
    }

    return ESP_OK;
}

static void log_rpc_parse_failure(const char *host, const char *via, esp_err_t http_err, int status,
                                  const char *body)
{
    if (body != NULL && body[0] != '\0') {
        ESP_LOGW(TAG, "rpc parse failed host=%s via=%s http=%s status=%d body=\"%.96s\"", host, via,
                 esp_err_to_name(http_err), status, body);
    } else {
        ESP_LOGW(TAG, "rpc parse failed host=%s via=%s http=%s status=%d body=(empty)", host, via,
                 esp_err_to_name(http_err), status);
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

static esp_err_t http_request_timeout(esp_http_client_method_t method, const char *url, const char *post_payload,
                                      char *body, size_t body_len, int *out_status, int timeout_ms)
{
    if (!prepare_http_transaction()) {
        return ESP_ERR_TIMEOUT;
    }

    shelly_http_body_t acc = {
        .body = body,
        .body_cap = body_len,
        .body_len = 0,
    };
    if (body != NULL && body_len > 0) {
        body[0] = '\0';
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = timeout_ms,
        .event_handler = shelly_http_event_handler,
        .user_data = &acc,
    };
    apply_shelly_auth(&config);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        finish_http_transaction();
        return ESP_ERR_NO_MEM;
    }

    if (method == HTTP_METHOD_POST && post_payload != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_payload, (int)strlen(post_payload));
    }

    const esp_err_t err = esp_http_client_perform(client);
    if (out_status != NULL) {
        *out_status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    finish_http_transaction();
    return err;
}

static esp_err_t http_get_body_timeout(const char *url, char *body, size_t body_len, int *out_status,
                                       int timeout_ms)
{
    return http_request_timeout(HTTP_METHOD_GET, url, NULL, body, body_len, out_status, timeout_ms);
}

static esp_err_t http_get_body(const char *url, char *body, size_t body_len, int *out_status)
{
    return http_get_body_timeout(url, body, body_len, out_status, HTTP_TIMEOUT_MS);
}

static esp_err_t http_post_json_body_timeout(const char *url, const char *payload, char *body, size_t body_len,
                                             int *out_status, int timeout_ms)
{
    return http_request_timeout(HTTP_METHOD_POST, url, payload, body, body_len, out_status, timeout_ms);
}

static esp_err_t http_post_json_body(const char *url, const char *payload, char *body, size_t body_len,
                                     int *out_status)
{
    return http_post_json_body_timeout(url, payload, body, body_len, out_status, HTTP_TIMEOUT_MS);
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

static bool json_find_bool(const char *json, const char *key, bool *out)
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
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }

    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

static uint16_t watts_from_power(float power)
{
    if (power < 0.0f) {
        power = 0.0f;
    }
    return (uint16_t)(power + 0.5f);
}

static void parse_switch_status_from_json(const char *json, shelly_plug_status_t *status)
{
    if (json == NULL || status == NULL || json[0] == '\0') {
        return;
    }

    const char *result = strstr(json, "\"result\"");
    const char *search = result != NULL ? result : json;

    float apower = 0.0f;
    if (json_find_float(search, "apower", &apower)) {
        status->watts_valid = true;
        status->watts = watts_from_power(apower);
    }

    bool output_on = false;
    if (json_find_bool(search, "output", &output_on)) {
        status->output_valid = true;
        status->output_on = output_on;
    }
}

static bool switch_status_response_ok(int status, const char *response, const shelly_plug_status_t *parsed)
{
    if (parsed == NULL || (!parsed->watts_valid && !parsed->output_valid)) {
        return false;
    }

    return (status >= 200 && status < 300) || (response != NULL && response[0] == '{');
}

static esp_err_t try_rpc_switch_status_post(const char *host, shelly_plug_status_t *status)
{
    char url[160];
    char response[RESPONSE_BUF_SIZE];
    int http_status = 0;

    snprintf(url, sizeof(url), "http://%s/rpc", host);
    static const char payload[] = "{\"id\":1,\"method\":\"Switch.GetStatus\",\"params\":{\"id\":0}}";
    const esp_err_t err = http_post_json_body_timeout(url, payload, response, sizeof(response), &http_status,
                                                    HTTP_POWER_TIMEOUT_MS);
    if (http_status == 401) {
        log_http_status_issue(host, "rpc status read", http_status);
        return ESP_ERR_INVALID_STATE;
    }

    shelly_plug_status_t parsed = {0};
    parse_switch_status_from_json(response, &parsed);
    if (switch_status_response_ok(http_status, response, &parsed)) {
        *status = parsed;
        return ESP_OK;
    }

    log_rpc_parse_failure(host, "POST Switch.GetStatus", err, http_status, response);
    return ESP_FAIL;
}

static esp_err_t try_rpc_switch_status_get(const char *host, shelly_plug_status_t *status)
{
    char url[192];
    char response[RESPONSE_BUF_SIZE];
    int http_status = 0;

    snprintf(url, sizeof(url), "http://%s/rpc/Switch.GetStatus?id=0", host);
    const esp_err_t err =
        http_get_body_timeout(url, response, sizeof(response), &http_status, HTTP_POWER_TIMEOUT_MS);
    if (http_status == 401) {
        log_http_status_issue(host, "rpc status read", http_status);
        return ESP_ERR_INVALID_STATE;
    }

    shelly_plug_status_t parsed = {0};
    parse_switch_status_from_json(response, &parsed);
    if (switch_status_response_ok(http_status, response, &parsed)) {
        *status = parsed;
        return ESP_OK;
    }

    log_rpc_parse_failure(host, "GET Switch.GetStatus", err, http_status, response);
    return err != ESP_OK ? err : ESP_FAIL;
}

static esp_err_t shelly_rpc_get_switch_status(const char *host, shelly_plug_status_t *status)
{
    /* Gen2/3/4: GET /rpc/Switch.GetStatus?id=0 (documented for Plug US Gen4). */
    if (try_rpc_switch_status_get(host, status) == ESP_OK) {
        api_cache_store(host, SHELLY_API_RPC);
        return ESP_OK;
    }

    if (try_rpc_switch_status_post(host, status) == ESP_OK) {
        api_cache_store(host, SHELLY_API_RPC);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "rpc status read failed host=%s", host);
    return ESP_FAIL;
}

static esp_err_t shelly_rpc_switch_set_get(const char *host, bool on, int *out_status)
{
    char url[192];
    snprintf(url, sizeof(url), "http://%s/rpc/Switch.Set?id=0&on=%s", host, on ? "true" : "false");
    return http_get_body(url, NULL, 0, out_status);
}

static esp_err_t shelly_rpc_switch_set_post(const char *host, bool on, int *out_status)
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
    if (s_exclusive_filter && plug != AQUAPILOT_SHELLY_FILTER) {
        return ESP_ERR_INVALID_STATE;
    }

    char host[AQUAPILOT_SHELLY_ADDR_MAX];
    if (!host_for_plug(plug, host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    int status = 0;
    esp_err_t err = shelly_rpc_switch_set_get(host, on, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "plug %d %s (rpc) host=%s status=%d", (int)plug, on ? "on" : "off", host, status);
        return ESP_OK;
    }
    if (status == 401) {
        log_http_status_issue(host, "plug set", status);
    }

    err = shelly_rpc_switch_set_post(host, on, &status);
    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "plug %d %s (rpc) host=%s status=%d", (int)plug, on ? "on" : "off", host, status);
        return ESP_OK;
    }
    if (status == 401) {
        log_http_status_issue(host, "plug set", status);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "plug %d %s failed host=%s err=%s status=%d", (int)plug, on ? "on" : "off", host,
             esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
}

esp_err_t shelly_client_get_plug_status(aquapilot_shelly_plug_t plug, shelly_plug_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *status = (shelly_plug_status_t){0};

    if (s_exclusive_filter && plug != AQUAPILOT_SHELLY_FILTER) {
        return ESP_ERR_INVALID_STATE;
    }

    char host[AQUAPILOT_SHELLY_ADDR_MAX];
    if (!host_for_plug(plug, host, sizeof(host))) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)api_cache_lookup(host);
    return shelly_rpc_get_switch_status(host, status);
}

esp_err_t shelly_client_get_plug_power_watts(aquapilot_shelly_plug_t plug, uint16_t *watts)
{
    if (watts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    shelly_plug_status_t status = {0};
    const esp_err_t err = shelly_client_get_plug_status(plug, &status);
    if (err != ESP_OK || !status.watts_valid) {
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    *watts = status.watts;
    return ESP_OK;
}

esp_err_t shelly_client_get_plug_switch_on(aquapilot_shelly_plug_t plug, bool *on)
{
    if (on == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    shelly_plug_status_t status = {0};
    const esp_err_t err = shelly_client_get_plug_status(plug, &status);
    if (err != ESP_OK || !status.output_valid) {
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    *on = status.output_on;
    return ESP_OK;
}

esp_err_t shelly_client_heater_plug_off(void)
{
    if (s_exclusive_filter) {
        return ESP_ERR_INVALID_STATE;
    }

    return shelly_client_plug_set(AQUAPILOT_SHELLY_HEATER, false);
}

void shelly_client_set_exclusive_filter_mode(bool enabled)
{
    if (enabled) {
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(HTTP_LOCK_MS + 1000);
        while (xTaskGetTickCount() < deadline) {
            if (lan_http_acquire(pdMS_TO_TICKS(50))) {
                lan_http_release();
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        s_exclusive_filter = true;
        ESP_LOGI(TAG, "exclusive filter mode ON (other Shelly polling paused)");
        return;
    }

    s_exclusive_filter = false;
    ESP_LOGI(TAG, "exclusive filter mode OFF");
}

bool shelly_client_is_exclusive_filter_mode(void)
{
    return s_exclusive_filter;
}
