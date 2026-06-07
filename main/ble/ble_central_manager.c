#include "ble_central_manager.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ble_central";

typedef struct {
    ble_central_driver_reg_t reg;
    bool registered;
} ble_central_slot_t;

static ble_central_slot_t s_slots[BLE_CENTRAL_DRV_COUNT];
static bool s_host_ready;
static bool s_inited;
static uint8_t s_session_owner;
static uint8_t s_conn_owner;
static uint16_t s_conn_handle;
static uint8_t s_own_addr_type;
static bool s_light_exclusive;

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        s_host_ready = false;
        return;
    }

    s_host_ready = true;
    ESP_LOGI(TAG, "NimBLE host synced");

    for (uint8_t i = 0; i < BLE_CENTRAL_DRV_COUNT; i++) {
        if (s_slots[i].registered && s_slots[i].reg.on_sync_fn != NULL) {
            s_slots[i].reg.on_sync_fn(s_slots[i].reg.on_sync_arg);
        }
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
    s_host_ready = false;
    s_conn_owner = BLE_CENTRAL_DRV_COUNT;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_session_owner = BLE_CENTRAL_DRV_COUNT;
}

esp_err_t ble_central_manager_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    memset(s_slots, 0, sizeof(s_slots));
    s_session_owner = BLE_CENTRAL_DRV_COUNT;
    s_conn_owner = BLE_CENTRAL_DRV_COUNT;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(ble_host_task);

    s_inited = true;
    ESP_LOGI(TAG, "BLE central manager initialized");
    return ESP_OK;
}

esp_err_t ble_central_manager_register(uint8_t driver_id, const ble_central_driver_reg_t *reg)
{
    if (driver_id >= BLE_CENTRAL_DRV_COUNT || reg == NULL || reg->gap_handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_slots[driver_id].reg = *reg;
    s_slots[driver_id].registered = true;
    ESP_LOGI(TAG, "registered driver %u (%s)", (unsigned)driver_id, reg->name != NULL ? reg->name : "?");
    return ESP_OK;
}

void ble_central_manager_set_driver_enabled(uint8_t driver_id, bool enabled)
{
    if (driver_id >= BLE_CENTRAL_DRV_COUNT) {
        return;
    }
    s_slots[driver_id].reg.enabled = enabled;
    if (!enabled && s_session_owner == driver_id) {
        ble_central_manager_release_session(driver_id);
    }
    if (!enabled && s_conn_owner == driver_id) {
        ble_central_manager_disconnect_active();
    }
}

bool ble_central_manager_is_ready(void)
{
    return s_inited && s_host_ready;
}

uint8_t ble_central_manager_connection_owner(void)
{
    return s_conn_owner;
}

uint8_t ble_central_manager_session_owner(void)
{
    return s_session_owner;
}

void ble_central_manager_cancel_discovery(void)
{
    ble_gap_disc_cancel();
}

void ble_central_manager_set_light_exclusive(bool active)
{
    s_light_exclusive = active;
}

bool ble_central_manager_is_light_exclusive(void)
{
    return s_light_exclusive;
}

bool ble_central_manager_handoff_to(uint8_t driver_id)
{
    if (driver_id >= BLE_CENTRAL_DRV_COUNT || !s_slots[driver_id].registered || !s_slots[driver_id].reg.enabled) {
        return false;
    }
    if (!ble_central_manager_is_ready()) {
        return false;
    }

    ble_central_manager_cancel_discovery();

    if (s_conn_owner < BLE_CENTRAL_DRV_COUNT && s_conn_owner != driver_id) {
        ble_central_manager_disconnect_active();
    }

    s_session_owner = driver_id;
    return true;
}

bool ble_central_manager_request_session(uint8_t driver_id)
{
    if (driver_id >= BLE_CENTRAL_DRV_COUNT || !s_slots[driver_id].registered || !s_slots[driver_id].reg.enabled) {
        return false;
    }
    if (!ble_central_manager_is_ready()) {
        return false;
    }

    if (s_session_owner < BLE_CENTRAL_DRV_COUNT && s_session_owner != driver_id) {
        const uint8_t current_prio = s_slots[s_session_owner].reg.priority;
        const uint8_t request_prio = s_slots[driver_id].reg.priority;
        if (request_prio < current_prio) {
            return false;
        }
        ble_central_manager_cancel_discovery();
        if (s_conn_owner == s_session_owner) {
            ble_central_manager_disconnect_active();
        }
        ble_central_manager_release_session(s_session_owner);
    }

    s_session_owner = driver_id;
    return true;
}

void ble_central_manager_release_session(uint8_t driver_id)
{
    if (s_session_owner == driver_id) {
        s_session_owner = BLE_CENTRAL_DRV_COUNT;
    }
}

void ble_central_manager_claim_connection(uint8_t driver_id, uint16_t conn_handle)
{
    if (driver_id >= BLE_CENTRAL_DRV_COUNT) {
        return;
    }
    s_conn_owner = driver_id;
    s_conn_handle = conn_handle;
    s_session_owner = driver_id;
}

void ble_central_manager_clear_connection(uint8_t driver_id)
{
    if (s_conn_owner == driver_id) {
        s_conn_owner = BLE_CENTRAL_DRV_COUNT;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    if (s_session_owner == driver_id) {
        s_session_owner = BLE_CENTRAL_DRV_COUNT;
    }
}

void ble_central_manager_disconnect_active(void)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static uint8_t gap_event_target(const struct ble_gap_event *event)
{
    if (event == NULL) {
        return BLE_CENTRAL_DRV_COUNT;
    }

    if (event->type == BLE_GAP_EVENT_DISC) {
        return s_session_owner;
    }
    if (s_conn_owner < BLE_CENTRAL_DRV_COUNT) {
        return s_conn_owner;
    }
    return s_session_owner;
}

int ble_central_manager_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    const uint8_t target = gap_event_target(event);
    if (target >= BLE_CENTRAL_DRV_COUNT || !s_slots[target].registered || s_slots[target].reg.gap_handler == NULL) {
        return 0;
    }

    int rc = s_slots[target].reg.gap_handler(event, s_slots[target].reg.gap_arg);

    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status == 0) {
            ble_central_manager_claim_connection(target, event->connect.conn_handle);
        } else if (s_session_owner == target) {
            ble_central_manager_clear_connection(target);
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        if (s_conn_owner == target) {
            ble_central_manager_clear_connection(target);
        }
    }

    return rc;
}

void ble_central_manager_tick(void)
{
    if (!ble_central_manager_is_ready()) {
        return;
    }

    /* During light poll, give the Fluval driver every tick. */
    if (s_light_exclusive) {
        if (s_slots[BLE_CENTRAL_DRV_LIGHT].registered && s_slots[BLE_CENTRAL_DRV_LIGHT].reg.tick_fn != NULL &&
            s_slots[BLE_CENTRAL_DRV_LIGHT].reg.enabled) {
            s_slots[BLE_CENTRAL_DRV_LIGHT].reg.tick_fn(s_slots[BLE_CENTRAL_DRV_LIGHT].reg.tick_arg);
        }
        return;
    }

    for (uint8_t i = 0; i < BLE_CENTRAL_DRV_COUNT; i++) {
        if (!s_slots[i].registered || s_slots[i].reg.tick_fn == NULL || !s_slots[i].reg.enabled) {
            continue;
        }
        s_slots[i].reg.tick_fn(s_slots[i].reg.tick_arg);
    }
}
