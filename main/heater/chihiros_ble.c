#include "chihiros_ble.h"

#include <stdio.h>
#include <string.h>

#include "ble/ble_central_manager.h"
#include "chihiros_heater_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

static const char *TAG = "chihiros_ble";

static ble_uuid_any_t NUS_SVC_UUID;
static ble_uuid_any_t NUS_RX_UUID;
static ble_uuid_any_t NUS_TX_UUID;

static const uint8_t INIT_1[] = {0x5a, 0x01, 0x06, 0x00, 0x02, 0x04, 0x01, 0x00};

static bool s_uuids_inited;

static void init_nus_uuids(void)
{
    if (s_uuids_inited) {
        return;
    }
    ble_uuid_from_str(&NUS_SVC_UUID, "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    ble_uuid_from_str(&NUS_RX_UUID, "6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    ble_uuid_from_str(&NUS_TX_UUID, "6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    s_uuids_inited = true;
}

static char s_name_prefix[8] = "DYH1";
static chihiros_status_t s_status;
static SemaphoreHandle_t s_mutex;
static bool s_inited;
static bool s_enabled;
static bool s_connect_requested;

#define INIT_POST_SUBSCRIBE_DELAY_US (300 * 1000ULL)

static uint16_t s_conn_handle;
static uint16_t s_nus_rx_handle;
static uint16_t s_nus_tx_handle;
static uint16_t s_nus_tx_cccd_handle;
static bool s_cccd_write_active;
static bool s_connect_pending;

static char s_adv_name[32];
static ble_addr_t s_adv_addr;
static bool s_have_adv_target;
static uint32_t s_backoff_ms;
static uint64_t s_next_action_us;
typedef enum {
    INIT_IDLE = 0,
    INIT_WAIT_AFTER_SUBSCRIBE,
    INIT_AWAIT_1_RSP,
    INIT_DONE,
} init_phase_t;

static init_phase_t s_init_phase;
static uint64_t s_init_phase_us;
static uint32_t s_init_done_ms;

static void maybe_finish_discovery(void);

static void log_svc_uuid(const struct ble_gatt_svc *svc)
{
    if (svc == NULL) {
        return;
    }
    char buf[BLE_UUID_STR_LEN];
    ESP_LOGI(TAG, "discovered service: %s handles=0x%04x-0x%04x",
             ble_uuid_to_str(&svc->uuid.u, buf), svc->start_handle, svc->end_handle);
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

static int write_no_rsp(const uint8_t *data, size_t len)
{
    if (!s_status.connected || s_nus_rx_handle == 0) {
        return -1;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return -1;
    }
    return ble_gattc_write_no_rsp(s_conn_handle, s_nus_rx_handle, om);
}

static void init_phase_begin(init_phase_t phase, uint64_t delay_us)
{
    s_init_phase = phase;
    s_init_phase_us = esp_timer_get_time() + delay_us;
}

static int init1_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;

    if (error != NULL && error->status != 0) {
        ESP_LOGW(TAG, "INIT_1 write failed=%d", error->status);
        init_phase_begin(INIT_WAIT_AFTER_SUBSCRIBE, 500 * 1000ULL);
        return 0;
    }
    ESP_LOGI(TAG, "INIT_1 acknowledged");
    s_init_phase = INIT_DONE;
    s_init_done_ms = now_ms();
    ESP_LOGI(TAG, "status wake sent, waiting for notifications");
    return 0;
}

static int write_with_response(const uint8_t *data, size_t len,
                               ble_gatt_attr_fn *cb, void *cb_arg)
{
    if (!s_status.connected || s_nus_rx_handle == 0) {
        return -1;
    }
    return ble_gattc_write_flat(s_conn_handle, s_nus_rx_handle, data, len, cb, cb_arg);
}

static void run_init_sequence_step(void)
{
    const uint64_t now = esp_timer_get_time();
    if (now < s_init_phase_us) {
        return;
    }

    switch (s_init_phase) {
    case INIT_WAIT_AFTER_SUBSCRIBE:
        log_hex("WRITE INIT_1", INIT_1, sizeof(INIT_1));
        if (write_with_response(INIT_1, sizeof(INIT_1), init1_write_cb, NULL) != 0) {
            ESP_LOGW(TAG, "INIT_1 write rejected (GATT busy?), retrying");
            init_phase_begin(INIT_WAIT_AFTER_SUBSCRIBE, 500 * 1000ULL);
        } else {
            s_init_phase = INIT_AWAIT_1_RSP;
        }
        break;
    case INIT_AWAIT_1_RSP:
        break;
    default:
        break;
    }
}

static int send_init_sequence(void)
{
    init_phase_begin(INIT_WAIT_AFTER_SUBSCRIBE, INIT_POST_SUBSCRIBE_DELAY_US);
    return 0;
}

static void handle_notify(struct os_mbuf *om)
{
    if (om == NULL) {
        ESP_LOGW(TAG, "notify with null mbuf");
        return;
    }

    uint8_t buf[64] = {0};
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

    chihiros_pkt_status_t decoded = {0};
    if (chihiros_decode_status_packet(buf, copy_len, &decoded)) {
        state_lock();
        s_status.status_valid = true;
        s_status.current_temp_c = decoded.current_temp_c;
        s_status.current_temp_f = decoded.current_temp_f;
        s_status.power_watts = decoded.watts;
        s_status.heating = decoded.heating;
        s_status.status_a = decoded.status_a;
        s_status.status_b = decoded.status_b;
        s_status.last_status_ms = (int64_t)now_ms();
        s_status.stale = false;
        state_unlock();
        ESP_LOGI(TAG, "status: %.1f F (%.1f C), %u W%s", decoded.current_temp_f, decoded.current_temp_c,
                 (unsigned)decoded.watts, decoded.heating ? " heating" : "");
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
    ESP_LOGI(TAG, "TX notifications enabled");
    send_init_sequence();
    return 0;
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

    if (ble_uuid_cmp(&chr->uuid.u, &NUS_RX_UUID.u) == 0) {
        s_nus_rx_handle = chr->val_handle;
        ESP_LOGI(TAG, "found NUS RX handle=0x%04x", s_nus_rx_handle);
    } else if (ble_uuid_cmp(&chr->uuid.u, &NUS_TX_UUID.u) == 0) {
        s_nus_tx_handle = chr->val_handle;
        s_nus_tx_cccd_handle = chr->val_handle + 1;
        ESP_LOGI(TAG, "found NUS TX handle=0x%04x CCCD=0x%04x", s_nus_tx_handle, s_nus_tx_cccd_handle);
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
        if (error != NULL && error->status == BLE_HS_EDONE && s_nus_rx_handle == 0) {
            ESP_LOGW(TAG, "GATT service discovery finished without NUS");
        }
        return 0;
    }

    if (ble_uuid_cmp(&svc->uuid.u, &NUS_SVC_UUID.u) == 0) {
        ESP_LOGI(TAG, "found NUS service");
        ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle, discover_chr_cb, NULL);
    } else {
        log_svc_uuid(svc);
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
    if (error != NULL && error->status != 0) {
        ESP_LOGW(TAG, "MTU exchange failed=%d", error->status);
    } else if (error != NULL && error->status == BLE_HS_EALREADY) {
        ESP_LOGI(TAG, "MTU already negotiated");
    } else {
        ESP_LOGI(TAG, "MTU negotiated=%u", (unsigned)mtu);
    }
    start_gatt_discovery(conn_handle);
    return 0;
}

static void start_mtu_and_discovery(uint16_t conn_handle)
{
    int rc = ble_gattc_exchange_mtu(conn_handle, mtu_exchange_cb, NULL);
    if (rc == BLE_HS_EALREADY) {
        ESP_LOGI(TAG, "MTU already negotiated");
        start_gatt_discovery(conn_handle);
    } else if (rc != 0) {
        ESP_LOGW(TAG, "exchange_mtu rc=%d", rc);
        start_gatt_discovery(conn_handle);
    }
}

static void maybe_finish_discovery(void)
{
    if (s_nus_rx_handle == 0 || s_nus_tx_handle == 0 || s_nus_tx_cccd_handle == 0) {
        return;
    }

    if (s_nus_tx_cccd_handle != 0 && !s_status.subscribed && !s_cccd_write_active) {
        s_cccd_write_active = true;
        uint8_t cccd_val[2] = {0x01, 0x00};
        ESP_LOGI(TAG, "subscribing on CCCD handle=0x%04x", s_nus_tx_cccd_handle);
        int rc = ble_gattc_write_flat(s_conn_handle, s_nus_tx_cccd_handle, cccd_val, sizeof(cccd_val),
                                      subscribe_complete_cb, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "CCCD write rc=%d", rc);
            s_cccd_write_active = false;
        }
    }
}

static int chihiros_gap_event(struct ble_gap_event *event, void *arg)
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
        ESP_LOGI(TAG, "found heater: %s", s_adv_name);
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
        s_nus_rx_handle = 0;
        s_nus_tx_handle = 0;
        s_nus_tx_cccd_handle = 0;
        s_cccd_write_active = false;
        s_init_phase = INIT_IDLE;
        s_backoff_ms = 500;
        ESP_LOGI(TAG, "connected to %s", s_adv_name[0] ? s_adv_name : "heater");
        start_mtu_and_discovery(s_conn_handle);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected (reason=%d)", event->disconnect.reason);
        s_connect_pending = false;
        state_lock();
        s_status.connected = false;
        s_status.subscribed = false;
        s_status.status_valid = false;
        s_status.stale = false;
        state_unlock();
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_nus_rx_handle = 0;
        s_nus_tx_handle = 0;
        s_nus_tx_cccd_handle = 0;
        s_cccd_write_active = false;
        s_init_done_ms = 0;
        s_have_adv_target = false;
        s_init_phase = INIT_IDLE;
        if (s_backoff_ms < 30000) {
            s_backoff_ms *= 2;
        }
        s_status.disconnect_count++;
        s_next_action_us = esp_timer_get_time() + (uint64_t)s_backoff_ms * 1000ULL;
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        handle_notify(event->notify_rx.om);
        return 0;

    default:
        return 0;
    }
}

static int scan_start(void)
{
    if (!ble_central_manager_request_session(BLE_CENTRAL_DRV_HEATER)) {
        return BLE_HS_EBUSY;
    }

    ble_central_manager_cancel_discovery();

    struct ble_gap_disc_params params = {0};
    params.passive = 0;
    params.itvl = 0x0010;
    params.window = 0x0010;
    params.filter_duplicates = 1;

    s_have_adv_target = false;
    return ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, ble_central_manager_gap_event, NULL);
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
    conn_params.supervision_timeout = 0x0258; /* 600 * 10ms = 6 s (was 2.56s — too short for hosted GATT) */
    conn_params.min_ce_len = 0x0010;
    conn_params.max_ce_len = 0x0030;

    return ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &s_adv_addr, 30000, &conn_params, ble_central_manager_gap_event,
                           NULL);
}

static void chihiros_ble_tick_fn(void *arg)
{
    (void)arg;

    if (!s_inited || !s_enabled || !s_connect_requested) {
        return;
    }

    state_lock();
    if (s_status.connected && s_status.status_valid) {
        uint32_t age = now_ms() - (uint32_t)s_status.last_status_ms;
        bool stale = age > CHIHIROS_STATUS_STALE_MS;
        if (stale != s_status.stale) {
            s_status.stale = stale;
            if (stale) {
                ESP_LOGW(TAG, "heater status stale (%lu ms old)", (unsigned long)age);
            }
        }
    }
    bool connected = s_status.connected;
    state_unlock();

    uint64_t now_us = esp_timer_get_time();
    if (connected) {
        if (s_init_phase != INIT_IDLE && s_init_phase != INIT_DONE && s_init_phase != INIT_AWAIT_1_RSP) {
            run_init_sequence_step();
        } else if (s_status.subscribed && s_init_phase == INIT_DONE && !s_status.status_valid && s_init_done_ms != 0) {
            if (now_ms() - s_init_done_ms > 10000) {
                ESP_LOGW(TAG, "no status yet, retrying init sequence");
                s_init_done_ms = 0;
                s_init_phase = INIT_IDLE;
                send_init_sequence();
            }
        }
    }

    if (now_us < s_next_action_us) {
        return;
    }

    if (connected || s_connect_pending) {
        return;
    }

    if (ble_central_manager_is_light_exclusive() ||
        ble_central_manager_session_owner() == BLE_CENTRAL_DRV_LIGHT) {
        return;
    }

    if (!s_have_adv_target) {
        int rc = scan_start();
        if (rc != 0) {
            ESP_LOGW(TAG, "scan rc=%d", rc);
            if (rc == BLE_HS_EBUSY) {
                ble_central_manager_cancel_discovery();
            }
        }
        s_next_action_us = now_us + (uint64_t)s_backoff_ms * 1000ULL;
        return;
    }

    int rc = connect_to_found();
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

static void chihiros_on_sync(void *arg)
{
    (void)arg;
    s_connect_requested = true;
    s_next_action_us = esp_timer_get_time();
}

static esp_err_t chihiros_ble_init_once(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    if (!chihiros_protocol_run_selftests()) {
        ESP_LOGE(TAG, "protocol self-tests failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "protocol self-tests passed");

    init_nus_uuids();

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
            .name = "chihiros",
            .priority = BLE_CENTRAL_PRIO_HEATER,
            .enabled = false,
            .gap_handler = chihiros_gap_event,
            .gap_arg = NULL,
            .tick_fn = chihiros_ble_tick_fn,
            .tick_arg = NULL,
            .on_sync_fn = chihiros_on_sync,
            .on_sync_arg = NULL,
        };
        err = ble_central_manager_register(BLE_CENTRAL_DRV_HEATER, &reg);
        if (err != ESP_OK) {
            return err;
        }
        registered = true;
    }

    s_inited = true;
    return ESP_OK;
}

void chihiros_ble_set_name_prefix(const char *prefix)
{
    if (prefix == NULL || prefix[0] == '\0') {
        snprintf(s_name_prefix, sizeof(s_name_prefix), "DYH1");
    } else {
        snprintf(s_name_prefix, sizeof(s_name_prefix), "%s", prefix);
    }
}

esp_err_t chihiros_ble_start(void)
{
    esp_err_t err = chihiros_ble_init_once();
    if (err != ESP_OK) {
        return err;
    }

    s_enabled = true;
    ble_central_manager_set_driver_enabled(BLE_CENTRAL_DRV_HEATER, true);
    s_connect_requested = true;
    ESP_LOGI(TAG, "started (prefix=%s)", s_name_prefix);
    return ESP_OK;
}

esp_err_t chihiros_ble_stop(void)
{
    s_enabled = false;
    s_connect_requested = false;
    ble_central_manager_set_driver_enabled(BLE_CENTRAL_DRV_HEATER, false);
    return ESP_OK;
}

void chihiros_ble_request_reconnect(void)
{
    s_connect_requested = true;
    s_connect_pending = false;
    s_have_adv_target = false;
    s_backoff_ms = 500;
    s_next_action_us = esp_timer_get_time();
}

static void request_status_refresh(void)
{
    if (s_status.connected && s_status.subscribed) {
        s_init_phase = INIT_IDLE;
        s_init_done_ms = 0;
        send_init_sequence();
        return;
    }
    ESP_LOGD(TAG, "status refresh ignored (connected=%d subscribed=%d)", s_status.connected,
             s_status.subscribed);
}

bool chihiros_ble_get_status(chihiros_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    state_lock();
    *out = s_status;
    state_unlock();
    return true;
}

esp_err_t chihiros_ble_set_target_f(float target_f)
{
    if (!s_status.connected || s_nus_rx_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!chihiros_setpoint_f_allowed(target_f)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t pkt[11] = {0};
    if (!chihiros_make_setpoint_packet_f(target_f, CHIHIROS_SETPOINT_MIN_F, CHIHIROS_SETPOINT_MAX_F, pkt)) {
        return ESP_ERR_INVALID_ARG;
    }

    log_hex("WRITE setpoint", pkt, sizeof(pkt));
    if (write_with_response(pkt, sizeof(pkt), NULL, NULL) != 0) {
        return ESP_FAIL;
    }

    state_lock();
    s_status.setpoint_f_last_sent = target_f;
    state_unlock();
    return ESP_OK;
}

/* Used by heater_service for periodic poll. */
void chihiros_ble_request_status_refresh(void)
{
    request_status_refresh();
}

bool chihiros_ble_is_subscribed(void)
{
    return s_inited && s_status.connected && s_status.subscribed;
}

bool chihiros_ble_has_valid_status(void)
{
    return s_inited && s_status.status_valid && !s_status.stale;
}
