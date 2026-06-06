#pragma once

#include "esp_err.h"

esp_err_t aquapilot_nvs_init(void);

esp_err_t aquapilot_nvs_get_blob(const char *key, void *out, size_t *inout_size);
esp_err_t aquapilot_nvs_set_blob(const char *key, const void *data, size_t size);
