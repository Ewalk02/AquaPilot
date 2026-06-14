#include "feeder_callback_server.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "schedule/feeder_service.h"
#include "feeder_amount.h"

static const char *TAG = "feeder_cb";

static esp_err_t send_json(httpd_req_t *req, int status, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "400 Bad Request");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_feed_complete(httpd_req_t *req)
{
    char body[256];
    const int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(body)) {
        return send_json(req, 400, "{\"error\":\"bad request\"}");
    }

    int received = 0;
    while (received < total) {
        const int chunk = httpd_req_recv(req, body + received, total - received);
        if (chunk <= 0) {
            return send_json(req, 400, "{\"error\":\"bad request\"}");
        }
        received += chunk;
    }
    body[received] = '\0';

    uint16_t amount_tenths = 0;
    uint32_t steps = 0;

    cJSON *root = cJSON_Parse(body);
    if (root != NULL) {
        const cJSON *seconds_item = cJSON_GetObjectItem(root, "seconds");
        const cJSON *steps_item = cJSON_GetObjectItem(root, "steps");
        if (cJSON_IsNumber(seconds_item)) {
            feeder_amount_seconds_value_to_tenths(cJSON_GetNumberValue(seconds_item), &amount_tenths);
        }
        if (cJSON_IsNumber(steps_item)) {
            steps = (uint32_t)steps_item->valueint;
        }
        cJSON_Delete(root);
    }

    feeder_service_on_feed_complete(amount_tenths, steps);
    ESP_LOGI(TAG, "feeding complete from feeder (%.1f s, %u steps)",
             (double)feeder_amount_tenths_to_seconds(amount_tenths), (unsigned)steps);
    return send_json(req, 200, "{\"ok\":true}");
}

esp_err_t feeder_callback_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t complete_uri = {
        .uri = "/api/feeder/complete",
        .method = HTTP_POST,
        .handler = handle_feed_complete,
    };
    httpd_register_uri_handler(server, &complete_uri);

    ESP_LOGI(TAG, "callback server listening on port %d", config.server_port);
    return ESP_OK;
}
