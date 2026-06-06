#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "fluval_light_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    bool subscribed;
    bool status_valid;
    bool stale;
    fluval_mode_t mode;
    uint8_t pink;
    uint8_t blue;
    uint8_t cold_white;
    uint8_t white;
    uint8_t warm_white;
    uint8_t avg_output;
    int64_t last_status_ms;
    uint32_t disconnect_count;
} fluval_status_t;

esp_err_t fluval_ble_start(void);
esp_err_t fluval_ble_stop(void);
bool fluval_ble_get_status(fluval_status_t *out);
void fluval_ble_set_name_prefix(const char *prefix);
void fluval_ble_request_reconnect(void);
void fluval_ble_request_status(void);
void fluval_ble_request_poll_window(void);
bool fluval_ble_is_poll_window_active(void);
esp_err_t fluval_ble_set_manual(void);
esp_err_t fluval_ble_set_auto(void);
esp_err_t fluval_ble_set_channels(uint8_t pink, uint8_t blue, uint8_t cold_white, uint8_t white, uint8_t warm_white);
esp_err_t fluval_ble_set_all(uint8_t percent);
bool fluval_ble_is_subscribed(void);
bool fluval_ble_has_valid_status(void);

#ifdef __cplusplus
}
#endif
