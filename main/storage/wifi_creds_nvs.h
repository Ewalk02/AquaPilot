#pragma once

#include <stdbool.h>

#define AQUAPILOT_WIFI_SSID_MAX 32
#define AQUAPILOT_WIFI_PASS_MAX 64

typedef struct {
    char ssid[AQUAPILOT_WIFI_SSID_MAX + 1];
    char password[AQUAPILOT_WIFI_PASS_MAX + 1];
    bool stored;
} aquapilot_wifi_creds_t;

void aquapilot_wifi_creds_defaults(aquapilot_wifi_creds_t *out);
bool aquapilot_wifi_creds_load(aquapilot_wifi_creds_t *out);
bool aquapilot_wifi_creds_save(const aquapilot_wifi_creds_t *in);
