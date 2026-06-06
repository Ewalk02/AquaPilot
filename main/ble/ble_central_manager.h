#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "host/ble_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_CENTRAL_DRV_HEATER   0
#define BLE_CENTRAL_DRV_COUNT    1

#define BLE_CENTRAL_PRIO_HEATER  10

typedef int (*ble_central_gap_handler_t)(struct ble_gap_event *event, void *arg);

typedef struct {
    const char *name;
    uint8_t priority;
    bool enabled;
    ble_central_gap_handler_t gap_handler;
    void *gap_arg;
    void (*tick_fn)(void *arg);
    void *tick_arg;
    void (*on_sync_fn)(void *arg);
    void *on_sync_arg;
} ble_central_driver_reg_t;

esp_err_t ble_central_manager_init(void);
esp_err_t ble_central_manager_register(uint8_t driver_id, const ble_central_driver_reg_t *reg);
void ble_central_manager_set_driver_enabled(uint8_t driver_id, bool enabled);
bool ble_central_manager_is_ready(void);
uint8_t ble_central_manager_connection_owner(void);
bool ble_central_manager_request_session(uint8_t driver_id);
void ble_central_manager_release_session(uint8_t driver_id);
void ble_central_manager_claim_connection(uint8_t driver_id, uint16_t conn_handle);
void ble_central_manager_clear_connection(uint8_t driver_id);
void ble_central_manager_disconnect_active(void);
void ble_central_manager_tick(void);
int ble_central_manager_gap_event(struct ble_gap_event *event, void *arg);

#ifdef __cplusplus
}
#endif
