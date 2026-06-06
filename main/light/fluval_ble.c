#include "fluval_ble.h"

#include <stdio.h>
#include <string.h>

#include "ble/ble_central_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

static const char *TAG = "fluval_ble";

static ble_uuid_any_t FFF0_SVC_UUID;
static ble_uuid_any_t FFF1_NOTIFY_UUID;
static ble_uuid_any_t FFF2_WRITE_UUID;

static bool s_uuids_inited;
static char s_name_prefix[16] = "Plant4.0";
static fluval_status_t s_status;
static SemaphoreHandle_t s_mutex;
static bool s_inited;
static bool s_enabled;
static bool s_connect_requested;

static uint16_t s_conn_handle;
static uint16_t s_write_handle;
static uint16_t s_notify_handle;
static uint16_t s_notify_cccd_handle;
static bool s_cccd_write_active;
static bool s_connect_pending;

static char s_adv_name[32];
static ble_addr_t s_adv_addr;
static bool s_have_adv_target;
static uint32_t s_backoff_ms;
static uint64_t s_next_action_us;
static uint64_t s_last_status_query_us;
static uint64_t s_poll_window_until_us;
static bool s_poll_window_requested;
static bool s_release_after_poll;
static bool s_scan_active;

#define STATUS_QUERY_INTERVAL_US (10 * 1000 * 1000ULL)
#define POLL_WINDOW_US           (35 * 1000 * 1000ULL)
#define POLL_GATT_EXTENSION_US   (20 * 1000 * 1000ULL)

static bool poll_window_active(void);

static void init_uuids(void)
{
    if (s_uuids_inited) {
        return;
    }
    ble_uuid_from_str(&FFF0_SVC_UUID, "0000fff0-0000-1000-8000-00805f9b34fb");
    ble_uuid_from_str(&FFF1_NOTIFY_UUID, "0000fff1-0000-1000-8000-00805f9b34fb");
    ble_uuid_from_str(&FFF2_WRITE_UUID, "0000fff2-0000-1000-8000-00805f9b34fb");
    s_uuids_inited = true;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void state_lock(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void state_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

static bool name_has_prefix(const char *name, const char *prefix)
{
    if (name == NULL || prefix == NULL || prefix[0] == '\0') {
        return false;
    }
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

static void log_hex(const char *label, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    char line[192];
    size_t pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "%s (%u):", label, (unsigned)len);
    for (size_t i = 0; i < len && pos + 3 < sizeof(line); i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, " %02x", data[i]);
    }
    ESP_LOGI(TAG, "%s", line);
}

static int write_cmd(const uint8_t *data, size_t len)
{
    if (!s_status.connected || s_write_handle == 0) {
        return -1;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return -1;
    }
    return ble_gattc_write_no_rsp(s_conn_handle, s_write_handle, om);
}

static void send_status_query(void)
{
    uint8_t cmd[4];
    size_t cmd_len = fluval_build_status_query(cmd, sizeof(cmd));
    if (cmd_len == 0) {
        return;
    }
    log_hex("WRITE status query", cmd, cmd_len);
    if (write_cmd(cmd, cmd_len) != 0) {
        ESP_LOGW(TAG, "status query write failed");
    } else {
        s_last_status_query_us = esp_timer_get_time();
    }
}

static void handle_notify(struct os_mbuf *om)
{
    if (om == NULL) {
        return;
    }

    uint8_t buf[256] = {0};
    uint16_t copy_len = 0;
    int rc = ble_hs_mbuf_to_flat(om, buf, sizeof(buf), &copy_len);
    if (rc != 0 && rc != BLE_HS_EMSGSIZE) {
        ESP_LOGW(TAG, "notify mbuf read failed rc=%d", rc);
        return;
    }
    if (copy_len == 0) {
        return;
    }

    log_hex("NOTIFY", buf, copy_len);

    fluval_pkt_status_t decoded = {0};
    if (fluval_decode_status_packet(buf, copy_len, &decoded)) {
        state_lock();
        s_status.status_valid = true;
        s_status.mode = decoded.mode;
        s_status.pink = decoded.pink;
        s_status.blue = decoded.blue;
        s_status.cold_white = decoded.cold_white;
        s_status.white = decoded.white;
        s_status.warm_white = decoded.warm_white;
        s_status.avg_output = decoded.avg_output;
        s_status.last_status_ms = (int64_t)now_ms();
        s_status.stale = false;
        state_unlock();
        ESP_LOGI(TAG, "status: mode=%u output=%u%%", (unsigned)decoded.mode, (unsigned)decoded.avg_output);
        return;
    }

    if (fluval_decode_ack_packet(buf, copy_len)) {
        ESP_LOGI(TAG, "command acknowledged");
    }
}

static int subscribe_complete_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                 struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;

    if (error && error->status != 0) {
        ESP_LOGW(TAG, "CCCD failed=%d", error->status);
        s_cccd_write_active = false;
        return 0;
    }

    state_lock();
    if (s_status.subscribed) {
        state_unlock();
        return 0;
    }
    s_status.subscribed = true;
    state_unlock();
    s_cccd_write_active = false;
    ESP_LOGI(TAG, "FFF1 notifications enabled");
    send_status_query();
    return 0;
}

static void maybe_finish_discovery(void)
{
    if (s_write_handle == 0 || s_notify_handle == 0 || s_notify_cccd_handle == 0) {
        return;
    }

    if (!s_status.subscribed && !s_cccd_write_active) {
        s_cccd_write_active = true;
        uint8_t cccd_val[2] = {0x01, 0x00};
        int rc = ble_gattc_write_flat(s_conn_handle, s_notify_cccd_handle, cccd_val, sizeof(cccd_val),
                                      subscribe_complete_cb, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "CCCD write rc=%d", rc);
            s_cccd_write_active = false;
        }
    }
}

static int discover_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr,
                           void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error && error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "characteristic discovery error=%d", error->status);
        return 0;
    }
    if (chr == NULL) {
        if (error == NULL || error->status == 0 || error->status == BLE_HS_EDONE) {
            maybe_finish_discovery();
        }
        return 0;
    }

    if (ble_uuid_cmp(&chr->uuid.u, &FFF2_WRITE_UUID.u) == 0) {
        s_write_handle = chr->val_handle;
        ESP_LOGI(TAG, "found FFF2 write handle=0x%04x", s_write_handle);
    } else if (ble_uuid_cmp(&chr->uuid.u, &FFF1_NOTIFY_UUID.u) == 0) {
        s_notify_handle = chr->val_handle;
        s_notify_cccd_handle = chr->val_handle + 1;
        ESP_LOGI(TAG, "found FFF1 notify handle=0x%04x CCCD=0x%04x", s_notify_handle, s_notify_cccd_handle);
    }
    return 0;
}

static int discover_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc,
                           void *arg)
{
    (void)arg;

    if (error && error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "service discovery error=%d", error->status);
        return 0;
    }
    if (svc == NULL) {
        return 0;
    }

    if (ble_uuid_cmp(&svc->uuid.u, &FFF0_SVC_UUID.u) == 0) {
        ESP_LOGI(TAG, "found FFF0 service");
        ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle, discover_chr_cb, NULL);
    }
    return 0;
}

static void start_gatt_discovery(uint16_t conn_handle)
{
    ESP_LOGI(TAG, "starting GATT service discovery");
    int rc = ble_gattc_disc_all_svcs(conn_handle, discover_svc_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "disc_all_svcs rc=%d", rc);
    }
}

static int mtu_exchange_cb(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    (void)arg;
    if (error != NULL && error->status != 0 && error->status != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "MTU exchange failed=%d", error->status);
    }
    start_gatt_discovery(conn_handle);
    return 0;
}

static void start_mtu_and_discovery(uint16_t conn_handle)
{
    int rc = ble_gattc_exchange_mtu(conn_handle, mtu_exchange_cb, NULL);
    if (rc == BLE_HS_EALREADY || rc != 0) {
        start_gatt_discovery(conn_handle);
    }
}

static int fluval_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }
        if (fields.name == NULL || fields.name_len == 0) {
            return 0;
        }

        char tmp[sizeof(s_adv_name)] = {0};
        size_t copy = fields.name_len < sizeof(tmp) - 1 ? (size_t)fields.name_len : sizeof(tmp) - 1;
        memcpy(tmp, fields.name, copy);

        if (!name_has_prefix(tmp, s_name_prefix)) {
            return 0;
        }

        strncpy(s_adv_name, tmp, sizeof(s_adv_name) - 1);
        s_adv_addr = event->disc.addr;
        s_have_adv_target = true;
        ESP_LOGI(TAG, "found light: %s", s_adv_name);
        s_scan_active = false;
        ble_gap_disc_cancel();
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            s_connect_pending = false;
            state_lock();
            s_status.connected = false;
            s_status.subscribed = false;
            state_unlock();
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_have_adv_target = false;
            if (s_backoff_ms < 30000) {
                s_backoff_ms *= 2;
            }
            return 0;
        }

        s_connect_pending = false;
        s_conn_handle = event->connect.conn_handle;
        state_lock();
        s_status.connected = true;
        s_status.subscribed = false;
        s_status.status_valid = false;
        s_status.stale = false;
        state_unlock();
        s_write_handle = 0;
        s_notify_handle = 0;
        s_notify_cccd_handle = 0;
        s_cccd_write_active = false;
        s_backoff_ms = 500;
        ESP_LOGI(TAG, "connected to %s", s_adv_name[0] ? s_adv_name : "light");
        if (s_poll_window_until_us != 0) {
            const uint64_t extended = esp_timer_get_time() + POLL_GATT_EXTENSION_US;
            if (extended > s_poll_window_until_us) {
                s_poll_window_until_us = extended;
            }
        }
        start_mtu_and_discovery(s_conn_handle);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected (reason=%d)", event->disconnect.reason);
        s_connect_pending = false;
        s_scan_active = false;
        state_lock();
        s_status.connected = false;
        s_status.subscribed = false;
        state_unlock();
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_write_handle = 0;
        s_notify_handle = 0;
        s_notify_cccd_handle = 0;
        s_cccd_write_active = false;
        s_have_adv_target = false;
        if (s_backoff_ms < 30000) {
            s_backoff_ms *= 2;
        }
        s_status.disconnect_count++;
        s_next_action_us = esp_timer_get_time() + (uint64_t)s_backoff_ms * 1000ULL;
        if (!poll_window_active()) {
            ble_central_manager_release_session(BLE_CENTRAL_DRV_LIGHT);
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        handle_notify(event->notify_rx.om);
        return 0;

    default:
        return 0;
    }
}

static bool poll_window_active(void)
{
    return s_poll_window_requested || s_release_after_poll || s_poll_window_until_us != 0;
}

static void end_poll_window(void)
{
    s_release_after_poll = false;
    s_poll_window_until_us = 0;
    s_poll_window_requested = false;
    s_connect_requested = false;
    s_connect_pending = false;
    s_have_adv_target = false;
    s_scan_active = false;
    ble_central_manager_cancel_discovery();
    if (s_status.connected) {
        ble_central_manager_disconnect_active();
    }
    ble_central_manager_release_session(BLE_CENTRAL_DRV_LIGHT);
    ble_central_manager_set_light_exclusive(false);
}

static int scan_start(void)
{
    if (s_scan_active) {
        return 0;
    }

    if (ble_central_manager_session_owner() != BLE_CENTRAL_DRV_LIGHT) {
        if (!ble_central_manager_handoff_to(BLE_CENTRAL_DRV_LIGHT)) {
            return BLE_HS_EBUSY;
        }
    }

    struct ble_gap_disc_params params = {0};
    params.passive = 0;
    params.itvl = 0x0010;
    params.window = 0x0010;
    params.filter_duplicates = 1;

    s_have_adv_target = false;
    const int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, ble_central_manager_gap_event, NULL);
    if (rc == 0) {
        s_scan_active = true;
    }
    return rc;
}

static int connect_to_found(void)
{
    if (!s_have_adv_target) {
        return 0;
    }

    struct ble_gap_conn_params conn_params = {0};
    conn_params.scan_itvl = 0x0010;
    conn_params.scan_window = 0x0010;
    conn_params.itvl_min = 0x0010;
    conn_params.itvl_max = 0x0020;
    conn_params.latency = 0;
    conn_params.supervision_timeout = 0x0258;
    conn_params.min_ce_len = 0x0010;
    conn_params.max_ce_len = 0x0030;

    return ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &s_adv_addr, 30000, &conn_params, ble_central_manager_gap_event,
                           NULL);
}

static void maybe_begin_poll_window(uint64_t now_us)
{
    if (!s_poll_window_requested) {
        return;
    }

    if (!ble_central_manager_handoff_to(BLE_CENTRAL_DRV_LIGHT)) {
        return;
    }

    ESP_LOGI(TAG, "starting light poll window");
    ble_central_manager_set_light_exclusive(true);
    s_poll_window_requested = false;
    s_poll_window_until_us = now_us + POLL_WINDOW_US;
    s_release_after_poll = true;
    s_connect_requested = true;
    s_scan_active = false;
    s_backoff_ms = 500;
    s_next_action_us = now_us;
}

static void fluval_ble_tick_fn(void *arg)
{
    (void)arg;

    if (!s_inited || !s_enabled) {
        return;
    }

    const uint64_t now_us = esp_timer_get_time();
    maybe_begin_poll_window(now_us);

    if (!poll_window_active() && !s_connect_requested) {
        return;
    }

    state_lock();
    if (s_status.connected && s_status.status_valid) {
        const uint32_t age = now_ms() - (uint32_t)s_status.last_status_ms;
        const bool stale = age > FLUVAL_STATUS_STALE_MS;
        if (stale != s_status.stale) {
            s_status.stale = stale;
            if (stale) {
                ESP_LOGW(TAG, "light status stale (%lu ms old)", (unsigned long)age);
            }
        }
    }
    const bool connected = s_status.connected;
    const bool subscribed = s_status.subscribed;
    const bool status_valid = s_status.status_valid;
    state_unlock();

    if (connected && subscribed && status_valid && s_release_after_poll) {
        ESP_LOGI(TAG, "light status received, returning BLE to heater");
        end_poll_window();
        return;
    }

    if (s_release_after_poll && now_us >= s_poll_window_until_us) {
        ESP_LOGI(TAG, "poll window timed out, returning BLE to heater");
        end_poll_window();
        return;
    }

    if (connected && subscribed) {
        if (now_us - s_last_status_query_us >= STATUS_QUERY_INTERVAL_US) {
            send_status_query();
        }
        return;
    }

    if (!s_connect_requested) {
        return;
    }

    if (now_us < s_next_action_us) {
        return;
    }

    if (connected || s_connect_pending) {
        return;
    }

    if (!s_have_adv_target) {
        if (!s_scan_active) {
            const int rc = scan_start();
            if (rc != 0) {
                ESP_LOGW(TAG, "scan rc=%d", rc);
            }
        }
        s_next_action_us = now_us + (uint64_t)s_backoff_ms * 1000ULL;
        return;
    }

    const int rc = connect_to_found();
    if (rc == 0) {
        s_connect_pending = true;
    } else {
        s_have_adv_target = false;
        if (s_backoff_ms < 30000) {
            s_backoff_ms *= 2;
        }
    }
    s_next_action_us = now_us + (uint64_t)s_backoff_ms * 1000ULL;
}

static void fluval_on_sync(void *arg)
{
    (void)arg;
}

static esp_err_t fluval_ble_init_once(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    if (!fluval_protocol_run_selftests()) {
        ESP_LOGE(TAG, "protocol self-tests failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "protocol self-tests passed");

    init_uuids();

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ble_central_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    static bool registered;
    if (!registered) {
        ble_central_driver_reg_t reg = {
            .name = "fluval",
            .priority = BLE_CENTRAL_PRIO_LIGHT,
            .enabled = false,
            .gap_handler = fluval_gap_event,
            .gap_arg = NULL,
            .tick_fn = fluval_ble_tick_fn,
            .tick_arg = NULL,
            .on_sync_fn = fluval_on_sync,
            .on_sync_arg = NULL,
        };
        const esp_err_t err = ble_central_manager_register(BLE_CENTRAL_DRV_LIGHT, &reg);
        if (err != ESP_OK) {
            return err;
        }
        registered = true;
    }

    s_inited = true;
    return ESP_OK;
}

void fluval_ble_set_name_prefix(const char *prefix)
{
    if (prefix == NULL || prefix[0] == '\0') {
        snprintf(s_name_prefix, sizeof(s_name_prefix), "Plant4.0");
    } else {
        snprintf(s_name_prefix, sizeof(s_name_prefix), "%s", prefix);
    }
}

esp_err_t fluval_ble_start(void)
{
    const esp_err_t err = fluval_ble_init_once();
    if (err != ESP_OK) {
        return err;
    }

    s_enabled = true;
    ble_central_manager_set_driver_enabled(BLE_CENTRAL_DRV_LIGHT, true);
    s_connect_requested = false;
    ESP_LOGI(TAG, "started (prefix=%s, idle until poll window)", s_name_prefix);
    return ESP_OK;
}

esp_err_t fluval_ble_stop(void)
{
    s_enabled = false;
    s_connect_requested = false;
    ble_central_manager_set_driver_enabled(BLE_CENTRAL_DRV_LIGHT, false);
    return ESP_OK;
}

void fluval_ble_request_reconnect(void)
{
    s_connect_requested = true;
    s_connect_pending = false;
    s_have_adv_target = false;
    s_backoff_ms = 500;
    s_next_action_us = esp_timer_get_time();
}

void fluval_ble_request_status(void)
{
    send_status_query();
}

void fluval_ble_request_poll_window(void)
{
    s_poll_window_requested = true;
}

bool fluval_ble_is_poll_window_active(void)
{
    return poll_window_active() || s_connect_requested || s_connect_pending;
}

bool fluval_ble_get_status(fluval_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    state_lock();
    *out = s_status;
    state_unlock();
    return true;
}

esp_err_t fluval_ble_set_manual(void)
{
    uint8_t cmd[8];
    const size_t cmd_len = fluval_build_set_manual(cmd, sizeof(cmd));
    if (cmd_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status.connected || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    log_hex("WRITE manual", cmd, cmd_len);
    return write_cmd(cmd, cmd_len) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t fluval_ble_set_auto(void)
{
    uint8_t cmd[8];
    const size_t cmd_len = fluval_build_set_auto(cmd, sizeof(cmd));
    if (cmd_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status.connected || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    log_hex("WRITE auto", cmd, cmd_len);
    return write_cmd(cmd, cmd_len) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t fluval_ble_set_channels(uint8_t pink, uint8_t blue, uint8_t cold_white, uint8_t white, uint8_t warm_white)
{
    uint8_t cmd[24];
    const size_t cmd_len = fluval_build_set_channels(pink, blue, cold_white, white, warm_white, cmd, sizeof(cmd));
    if (cmd_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status.connected || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    log_hex("WRITE channels", cmd, cmd_len);
    if (write_cmd(cmd, cmd_len) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t fluval_ble_set_all(uint8_t percent)
{
    uint8_t cmd[24];
    const size_t cmd_len = fluval_build_set_all(percent, cmd, sizeof(cmd));
    if (cmd_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status.connected || s_write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    log_hex("WRITE set all", cmd, cmd_len);
    if (write_cmd(cmd, cmd_len) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool fluval_ble_is_subscribed(void)
{
    return s_inited && s_status.connected && s_status.subscribed;
}

bool fluval_ble_has_valid_status(void)
{
    return s_inited && s_status.status_valid && !s_status.stale;
}
