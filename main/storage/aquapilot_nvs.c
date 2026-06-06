#include "aquapilot_nvs.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "aquapilot_nvs";
static const char *NVS_NAMESPACE = "aquapilot";

esp_err_t aquapilot_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition requires erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t aquapilot_nvs_get_blob(const char *key, void *out, size_t *inout_size)
{
    if (key == NULL || out == NULL || inout_size == NULL || *inout_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvh;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvh);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvh, key, out, inout_size);
    nvs_close(nvh);
    return err;
}

esp_err_t aquapilot_nvs_set_blob(const char *key, const void *data, size_t size)
{
    if (key == NULL || data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvh;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvh);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(nvh, key, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(nvh);
    }
    nvs_close(nvh);
    return err;
}
