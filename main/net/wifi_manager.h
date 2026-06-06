#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    AQUAPILOT_WIFI_STATUS_UNAVAILABLE = 0,
    AQUAPILOT_WIFI_STATUS_CREDENTIALS_MISSING,
    AQUAPILOT_WIFI_STATUS_NOT_STARTED,
    AQUAPILOT_WIFI_STATUS_CONNECTING,
    AQUAPILOT_WIFI_STATUS_CONNECTED,
    AQUAPILOT_WIFI_STATUS_DISCONNECTED,
} aquapilot_wifi_status_kind_t;

typedef struct {
    aquapilot_wifi_status_kind_t kind;
    uint32_t reconnect_attempt;
    int last_disconnect_reason;
    char reason_text[64];
} aquapilot_wifi_status_t;

bool aquapilot_wifi_init(void);
bool aquapilot_wifi_start_sta(void);
bool aquapilot_wifi_stack_ready(void);
bool aquapilot_wifi_is_connected(void);

void aquapilot_wifi_get_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len);

/** Save to NVS and reconnect (safe from UI; work runs in background task). */
void aquapilot_wifi_apply_credentials_async(const char *ssid, const char *password);

void aquapilot_wifi_get_status(aquapilot_wifi_status_t *out);
const char *aquapilot_wifi_status_text(void);

bool aquapilot_wifi_get_sta_ip(char *buf, size_t len);
