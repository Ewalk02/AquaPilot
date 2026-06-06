#include "wifi_creds_nvs.h"

#include <string.h>

#include "aquapilot_nvs.h"
#include "esp_log.h"

#ifndef CONFIG_ESP_WIFI_SSID
#define CONFIG_ESP_WIFI_SSID ""
#endif
#ifndef CONFIG_ESP_WIFI_PASSWORD
#define CONFIG_ESP_WIFI_PASSWORD ""
#endif

static const char *TAG = "wifi_creds";
static const char *NVS_KEY_WIFI_CREDS = "wifi_creds_v1";

void aquapilot_wifi_creds_defaults(aquapilot_wifi_creds_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->ssid, CONFIG_ESP_WIFI_SSID, sizeof(out->ssid) - 1);
    strncpy(out->password, CONFIG_ESP_WIFI_PASSWORD, sizeof(out->password) - 1);
    out->stored = false;
}

bool aquapilot_wifi_creds_load(aquapilot_wifi_creds_t *out)
{
    aquapilot_wifi_creds_defaults(out);

    size_t size = sizeof(*out);
    esp_err_t err = aquapilot_nvs_get_blob(NVS_KEY_WIFI_CREDS, out, &size);
    if (err != ESP_OK || size != sizeof(*out)) {
        return false;
    }

    out->ssid[sizeof(out->ssid) - 1] = '\0';
    out->password[sizeof(out->password) - 1] = '\0';
    return out->stored;
}

bool aquapilot_wifi_creds_save(const aquapilot_wifi_creds_t *in)
{
    if (in == NULL) {
        return false;
    }

    aquapilot_wifi_creds_t copy = *in;
    copy.stored = true;
    copy.ssid[sizeof(copy.ssid) - 1] = '\0';
    copy.password[sizeof(copy.password) - 1] = '\0';

    esp_err_t err = aquapilot_nvs_set_blob(NVS_KEY_WIFI_CREDS, &copy, sizeof(copy));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}
