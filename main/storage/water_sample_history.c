#include "water_sample_history.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "schedule/aquapilot_time.h"
#include "storage/sd_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "wsample";

#define FILE_MAGIC   0x57534D50u /* WSMP */
#define FILE_VERSION 1
#define INVALID_X100 INT16_MIN

typedef struct __attribute__((packed)) {
    int32_t epoch;
    int16_t ph_x100;
    int16_t ammonia_x100;
    int16_t nitrate_x100;
    int16_t nitrite_x100;
    int16_t kh_x10;
    int16_t gh_x10;
} water_sample_packed_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_count;
    uint32_t write_idx;
    uint32_t filled;
    water_sample_packed_t slots[WATER_SAMPLE_MAX_RECORDS];
} water_sample_file_t;

static water_sample_file_t s_store;
static SemaphoreHandle_t s_lock;
static bool s_sd_ready;
static char s_file_path[64];

static float x100_to_float(int16_t v)
{
    if (v == INVALID_X100) {
        return NAN;
    }
    return (float)v / 100.0f;
}

static float x10_to_float(int16_t v)
{
    if (v == INVALID_X100) {
        return NAN;
    }
    return (float)v / 10.0f;
}

static int16_t float_to_x100(float v)
{
    if (isnan(v) || v < -327.0f || v > 327.0f) {
        return INVALID_X100;
    }
    return (int16_t)lroundf(v * 100.0f);
}

static int16_t float_to_x10(float v)
{
    if (isnan(v) || v < -327.0f || v > 327.0f) {
        return INVALID_X100;
    }
    return (int16_t)lroundf(v * 10.0f);
}

static void store_defaults(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = FILE_MAGIC;
    s_store.version = FILE_VERSION;
    s_store.slot_count = WATER_SAMPLE_MAX_RECORDS;
    s_store.write_idx = 0;
    s_store.filled = 0;
    for (uint16_t i = 0; i < WATER_SAMPLE_MAX_RECORDS; i++) {
        s_store.slots[i].epoch = 0;
        s_store.slots[i].ph_x100 = INVALID_X100;
        s_store.slots[i].ammonia_x100 = INVALID_X100;
        s_store.slots[i].nitrate_x100 = INVALID_X100;
        s_store.slots[i].nitrite_x100 = INVALID_X100;
        s_store.slots[i].kh_x10 = INVALID_X100;
        s_store.slots[i].gh_x10 = INVALID_X100;
    }
}

static bool load_from_sd(void)
{
    water_sample_file_t loaded = {0};
    if (!aquapilot_sd_read_file(s_file_path, &loaded, sizeof(loaded))) {
        return false;
    }

    if (loaded.magic != FILE_MAGIC || loaded.version != FILE_VERSION ||
        loaded.slot_count != WATER_SAMPLE_MAX_RECORDS) {
        ESP_LOGW(TAG, "history file invalid or version mismatch");
        return false;
    }

    s_store = loaded;
    ESP_LOGI(TAG, "loaded %u water samples from SD", (unsigned)s_store.filled);
    return true;
}

static bool save_to_sd(void)
{
    if (!aquapilot_sd_is_mounted()) {
        return false;
    }

    if (!aquapilot_sd_write_file(s_file_path, &s_store, sizeof(s_store))) {
        ESP_LOGW(TAG, "failed to write %s", s_file_path);
        if (!aquapilot_sd_is_mounted()) {
            s_sd_ready = false;
        }
        return false;
    }

    return true;
}

static uint16_t chronological_index(uint16_t offset)
{
    if (s_store.filled < WATER_SAMPLE_MAX_RECORDS) {
        return offset;
    }
    return (uint16_t)((s_store.write_idx + offset) % WATER_SAMPLE_MAX_RECORDS);
}

static bool metric_valid(const water_sample_packed_t *slot, water_sample_metric_t metric)
{
    switch (metric) {
    case WATER_METRIC_PH:
        return slot->ph_x100 != INVALID_X100;
    case WATER_METRIC_AMMONIA:
        return slot->ammonia_x100 != INVALID_X100;
    case WATER_METRIC_NITRATE:
        return slot->nitrate_x100 != INVALID_X100;
    case WATER_METRIC_NITRITE:
        return slot->nitrite_x100 != INVALID_X100;
    case WATER_METRIC_KH:
        return slot->kh_x10 != INVALID_X100;
    case WATER_METRIC_GH:
        return slot->gh_x10 != INVALID_X100;
    default:
        return false;
    }
}

static float metric_value(const water_sample_packed_t *slot, water_sample_metric_t metric)
{
    switch (metric) {
    case WATER_METRIC_PH:
        return x100_to_float(slot->ph_x100);
    case WATER_METRIC_AMMONIA:
        return x100_to_float(slot->ammonia_x100);
    case WATER_METRIC_NITRATE:
        return x100_to_float(slot->nitrate_x100);
    case WATER_METRIC_NITRITE:
        return x100_to_float(slot->nitrite_x100);
    case WATER_METRIC_KH:
        return x10_to_float(slot->kh_x10);
    case WATER_METRIC_GH:
        return x10_to_float(slot->gh_x10);
    default:
        return NAN;
    }
}

esp_err_t water_sample_history_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    store_defaults();
    snprintf(s_file_path, sizeof(s_file_path), "%s/wsample.dat", BSP_SD_MOUNT_POINT);

    if (aquapilot_sd_is_mounted()) {
        if (!load_from_sd()) {
            store_defaults();
            s_sd_ready = save_to_sd();
            if (s_sd_ready) {
                ESP_LOGI(TAG, "new water sample file created at %s", s_file_path);
            } else {
                ESP_LOGW(TAG, "could not create %s on SD", s_file_path);
            }
        } else {
            s_sd_ready = true;
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted; water samples kept in RAM only");
        s_sd_ready = false;
    }

    ESP_LOGI(TAG, "water sample history started (%u slots)", (unsigned)WATER_SAMPLE_MAX_RECORDS);
    return ESP_OK;
}

bool water_sample_history_storage_ready(void)
{
    return s_sd_ready;
}

bool water_sample_history_try_attach_storage(void)
{
    if (s_sd_ready && aquapilot_sd_is_mounted()) {
        return true;
    }

    if (!aquapilot_sd_is_mounted()) {
        if (aquapilot_sd_mount() != ESP_OK && aquapilot_sd_remount() != ESP_OK) {
            return false;
        }
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }

    if (load_from_sd()) {
        s_sd_ready = true;
        xSemaphoreGive(s_lock);
        return true;
    }

    if (aquapilot_sd_remount() == ESP_OK && load_from_sd()) {
        s_sd_ready = true;
        xSemaphoreGive(s_lock);
        return true;
    }

    s_sd_ready = save_to_sd();
    xSemaphoreGive(s_lock);
    return s_sd_ready;
}

bool water_sample_history_add(const water_sample_record_t *record)
{
    if (record == NULL || record->epoch <= 0) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }

    water_sample_packed_t *slot = &s_store.slots[s_store.write_idx];
    slot->epoch = record->epoch;
    slot->ph_x100 = float_to_x100(record->ph);
    slot->ammonia_x100 = float_to_x100(record->ammonia_ppm);
    slot->nitrate_x100 = float_to_x100(record->nitrate_ppm);
    slot->nitrite_x100 = float_to_x100(record->nitrite_ppm);
    slot->kh_x10 = float_to_x10(record->kh_deg);
    slot->gh_x10 = float_to_x10(record->gh_deg);

    s_store.write_idx = (s_store.write_idx + 1) % WATER_SAMPLE_MAX_RECORDS;
    if (s_store.filled < WATER_SAMPLE_MAX_RECORDS) {
        s_store.filled++;
    }

    const bool saved = save_to_sd();
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "sample saved epoch=%ld", (long)record->epoch);
    return saved || !s_sd_ready;
}

const char *water_sample_metric_label(water_sample_metric_t metric)
{
    switch (metric) {
    case WATER_METRIC_PH:
        return "pH";
    case WATER_METRIC_AMMONIA:
        return "Ammonia (ppm)";
    case WATER_METRIC_NITRATE:
        return "Nitrate (ppm)";
    case WATER_METRIC_NITRITE:
        return "Nitrite (ppm)";
    case WATER_METRIC_KH:
        return "KH (deg)";
    case WATER_METRIC_GH:
        return "GH (deg)";
    default:
        return "?";
    }
}

static uint16_t collect_metric_points(water_sample_metric_t metric, int32_t *out_epochs, float *out_values,
                                      uint16_t max_points, bool apply_window)
{
    uint16_t count = 0;
    const time_t now = time(NULL);
    const int64_t window_start = (int64_t)now - WATER_SAMPLE_WINDOW_SEC;

    for (uint16_t i = 0; i < s_store.filled && count < max_points; i++) {
        const water_sample_packed_t *slot = &s_store.slots[chronological_index(i)];
        if (slot->epoch <= 0) {
            continue;
        }
        if (apply_window && aquapilot_time_is_ready() && (int64_t)slot->epoch < window_start) {
            continue;
        }
        if (!metric_valid(slot, metric)) {
            continue;
        }

        const float value = metric_value(slot, metric);
        if (isnan(value)) {
            continue;
        }

        out_epochs[count] = slot->epoch;
        out_values[count] = value;
        count++;
    }

    if (count > 1) {
        /* Sort parallel arrays by epoch using a simple insertion order via index sort. */
        for (uint16_t i = 0; i < count - 1; i++) {
            for (uint16_t j = i + 1; j < count; j++) {
                if (out_epochs[j] < out_epochs[i]) {
                    const int32_t tmp_e = out_epochs[i];
                    const float tmp_v = out_values[i];
                    out_epochs[i] = out_epochs[j];
                    out_values[i] = out_values[j];
                    out_epochs[j] = tmp_e;
                    out_values[j] = tmp_v;
                }
            }
        }
    }

    return count;
}

bool water_sample_history_get_metric_series(water_sample_metric_t metric, int32_t *out_epochs, float *out_values,
                                            uint16_t max_points, uint16_t *out_count)
{
    if (out_epochs == NULL || out_values == NULL || out_count == NULL || max_points == 0 ||
        metric < 0 || metric >= WATER_METRIC_COUNT) {
        return false;
    }

    *out_count = 0;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }

    *out_count = collect_metric_points(metric, out_epochs, out_values, max_points, true);
    if (*out_count == 0 && s_store.filled > 0) {
        *out_count = collect_metric_points(metric, out_epochs, out_values, max_points, false);
    }

    xSemaphoreGive(s_lock);
    return true;
}
