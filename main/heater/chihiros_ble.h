#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Live heater connection + latest decoded status for UI/diagnostics. */
typedef struct {
    bool connected;
    bool subscribed;
    bool status_valid;
    bool stale;

    float current_temp_c;
    float current_temp_f;
    float setpoint_f_last_sent;

    uint16_t power_watts;
    bool heating;

    uint8_t status_a;
    uint8_t status_b;

    int64_t last_status_ms;
    int disconnect_count;
} chihiros_status_t;

esp_err_t chihiros_ble_start(void);
esp_err_t chihiros_ble_stop(void);

esp_err_t chihiros_ble_set_target_f(float target_f);
bool chihiros_ble_get_status(chihiros_status_t *out);

void chihiros_ble_request_reconnect(void);
void chihiros_ble_request_status_refresh(void);
void chihiros_ble_set_name_prefix(const char *prefix);
bool chihiros_ble_is_subscribed(void);
bool chihiros_ble_has_valid_status(void);

#ifdef __cplusplus
}
#endif
