#include "temp_history.h"

#include "aquapilot_time.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "storage/sd_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "heater/heater_service.h"
#include "sensors/sht3x_sensor.h"
#include "storage/aquapilot_settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHART_GAP_VALUE INT32_MAX

static const char *TAG = "temp_hist";

#define FILE_MAGIC   0x544D5048u /* TMPH */
#define FILE_VERSION 1
#define SAMPLE_TICK_MS 60000
#define INVALID_TEMP_X10 INT16_MIN

typedef struct __attribute__((packed)) {
    int32_t epoch;
    int16_t tank_f_x10;
    int16_t ambient_f_x10;
} temp_hist_sample_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_count;
    uint32_t interval_sec;
    uint32_t write_idx;
    uint32_t filled;
    temp_hist_sample_t slots[TEMP_HISTORY_SLOTS];
} temp_hist_file_t;

static temp_hist_file_t s_store;
static SemaphoreHandle_t s_lock;
static bool s_sd_ready;
static int32_t s_last_bucket = -1;
static char s_file_path[64];

static int16_t float_to_x10(float temp_f)
{
    if (temp_f < -400.0f || temp_f > 400.0f) {
        return INVALID_TEMP_X10;
    }
    return (int16_t)(temp_f * 10.0f);
}

static float x10_to_float(int16_t temp_x10)
{
    if (temp_x10 == INVALID_TEMP_X10) {
        return 0.0f;
    }
    return (float)temp_x10 / 10.0f;
}

static void store_defaults(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = FILE_MAGIC;
    s_store.version = FILE_VERSION;
    s_store.slot_count = TEMP_HISTORY_SLOTS;
    s_store.interval_sec = TEMP_HISTORY_INTERVAL_SEC;
    s_store.write_idx = 0;
    s_store.filled = 0;
    for (uint16_t i = 0; i < TEMP_HISTORY_SLOTS; i++) {
        s_store.slots[i].epoch = 0;
        s_store.slots[i].tank_f_x10 = INVALID_TEMP_X10;
        s_store.slots[i].ambient_f_x10 = INVALID_TEMP_X10;
    }
}

static bool load_from_sd(void)
{
    temp_hist_file_t loaded = {0};
    if (!aquapilot_sd_read_file(s_file_path, &loaded, sizeof(loaded))) {
        return false;
    }

    if (loaded.magic != FILE_MAGIC || loaded.version != FILE_VERSION ||
        loaded.slot_count != TEMP_HISTORY_SLOTS || loaded.interval_sec != TEMP_HISTORY_INTERVAL_SEC) {
        ESP_LOGW(TAG, "history file invalid or version mismatch");
        return false;
    }

    s_store = loaded;
    ESP_LOGI(TAG, "loaded %u samples from SD", (unsigned)s_store.filled);
    return true;
}

static bool save_to_sd(void)
{
    if (!s_sd_ready || !aquapilot_sd_is_mounted()) {
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

static void record_sample_locked(int32_t epoch)
{
    temp_hist_sample_t *slot = &s_store.slots[s_store.write_idx];
    slot->epoch = epoch;

    float tank_f = 0.0f;
    if (heater_service_is_heater_online() && heater_service_get_temp_f(&tank_f)) {
        slot->tank_f_x10 = float_to_x10(tank_f);
    } else {
        slot->tank_f_x10 = INVALID_TEMP_X10;
    }

    if (sht3x_sensor_has_reading()) {
        slot->ambient_f_x10 = float_to_x10(sht3x_sensor_get_temp_f());
    } else {
        slot->ambient_f_x10 = INVALID_TEMP_X10;
    }

    s_store.write_idx = (s_store.write_idx + 1) % TEMP_HISTORY_SLOTS;
    if (s_store.filled < TEMP_HISTORY_SLOTS) {
        s_store.filled++;
    }

    save_to_sd();
    ESP_LOGD(TAG, "sample epoch=%ld tank=%d ambient=%d", (long)epoch, (int)slot->tank_f_x10,
             (int)slot->ambient_f_x10);
}

static void maybe_record_sample(void)
{
    bool logging_enabled = false;
    if (!aquapilot_settings_get_temp_graph_logging_enabled(&logging_enabled) || !logging_enabled) {
        return;
    }

    if (!aquapilot_time_is_ready()) {
        return;
    }

    const time_t now = time(NULL);
    const int32_t bucket = (int32_t)(now / (time_t)TEMP_HISTORY_INTERVAL_SEC);
    if (bucket == s_last_bucket) {
        return;
    }

    bool has_data = heater_service_is_heater_online() || sht3x_sensor_has_reading();
    if (!has_data) {
        return;
    }

    s_last_bucket = bucket;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        record_sample_locked((int32_t)now);
        xSemaphoreGive(s_lock);
    }
}

static void sampler_task(void *arg)
{
    (void)arg;

    while (true) {
        maybe_record_sample();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_TICK_MS));
    }
}

static uint16_t chronological_index(uint16_t offset)
{
    if (s_store.filled < TEMP_HISTORY_SLOTS) {
        return offset;
    }

    return (uint16_t)((s_store.write_idx + offset) % TEMP_HISTORY_SLOTS);
}

static int epoch_to_chart_index(int32_t epoch, time_t now)
{
    if (epoch <= 0) {
        return -1;
    }

    const int64_t window_start = (int64_t)now - (int64_t)TEMP_HISTORY_WINDOW_SEC;
    const int64_t offset = (int64_t)epoch - window_start;
    if (offset < 0) {
        return -1;
    }

    if (offset >= TEMP_HISTORY_WINDOW_SEC) {
        return (int)TEMP_HISTORY_SLOTS - 1;
    }

    return (int)(offset / TEMP_HISTORY_INTERVAL_SEC);
}

static void place_chart_value(int32_t *series, int16_t temp_x10, int32_t *y_min, int32_t *y_max)
{
    const int32_t rounded = (int32_t)lroundf(x10_to_float(temp_x10));
    *series = rounded;
    if (rounded < *y_min) {
        *y_min = rounded;
    }
    if (rounded > *y_max) {
        *y_max = rounded;
    }
}

esp_err_t temp_history_init(void)
{
    store_defaults();

    /* 8.3 name required (CONFIG_FATFS_LFN_NONE). */
    snprintf(s_file_path, sizeof(s_file_path), "%s/tmphist.dat", BSP_SD_MOUNT_POINT);

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (aquapilot_sd_is_mounted()) {
        s_sd_ready = true;
        if (!load_from_sd()) {
            store_defaults();
            if (save_to_sd()) {
                ESP_LOGI(TAG, "new history file created at %s", s_file_path);
            } else {
                ESP_LOGW(TAG, "could not create %s on SD", s_file_path);
                s_sd_ready = false;
            }
        }
        if (s_sd_ready) {
            ESP_LOGI(TAG, "SD storage at %s", s_file_path);
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted; history kept in RAM only");
    }

    if (xTaskCreate(sampler_task, "temp_hist", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "sampler task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "temperature history started (%u points / %u h)", (unsigned)TEMP_HISTORY_SLOTS,
             (unsigned)(TEMP_HISTORY_WINDOW_SEC / 3600));
    return ESP_OK;
}

bool temp_history_storage_ready(void)
{
    return s_sd_ready && aquapilot_sd_is_mounted();
}

uint16_t temp_history_point_count(void)
{
    return TEMP_HISTORY_SLOTS;
}

bool temp_history_get_chart_values(int32_t *out_tank, int32_t *out_ambient, uint16_t max_points,
                                   int32_t *out_y_min, int32_t *out_y_max)
{
    if (out_tank == NULL || out_ambient == NULL || max_points < TEMP_HISTORY_SLOTS) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }

    const uint16_t count = s_store.filled;
    const bool time_ready = aquapilot_time_is_ready();
    const time_t now = time_ready ? time(NULL) : 0;
    int32_t y_min = INT32_MAX;
    int32_t y_max = INT32_MIN;
    uint16_t placed = 0;

    for (uint16_t i = 0; i < TEMP_HISTORY_SLOTS; i++) {
        out_tank[i] = CHART_GAP_VALUE;
        out_ambient[i] = CHART_GAP_VALUE;
    }

    for (uint16_t i = 0; i < count; i++) {
        const temp_hist_sample_t *sample = &s_store.slots[chronological_index(i)];
        uint16_t out_idx;

        if (time_ready) {
            const int slot = epoch_to_chart_index(sample->epoch, now);
            if (slot < 0) {
                continue;
            }
            out_idx = (uint16_t)slot;
        } else {
            const uint16_t missing = TEMP_HISTORY_SLOTS - count;
            out_idx = (uint16_t)(missing + i);
        }

        if (sample->tank_f_x10 != INVALID_TEMP_X10) {
            place_chart_value(&out_tank[out_idx], sample->tank_f_x10, &y_min, &y_max);
            placed++;
        }

        if (sample->ambient_f_x10 != INVALID_TEMP_X10) {
            place_chart_value(&out_ambient[out_idx], sample->ambient_f_x10, &y_min, &y_max);
            placed++;
        }
    }

    xSemaphoreGive(s_lock);

    if (count == 0 || placed == 0) {
        if (out_y_min != NULL) {
            *out_y_min = 70;
        }
        if (out_y_max != NULL) {
            *out_y_max = 80;
        }
        return false;
    }

    if (y_min == INT32_MAX || y_max == INT32_MIN) {
        y_min = 70;
        y_max = 80;
    } else {
        y_min -= 2;
        y_max += 2;
        if (y_max <= y_min) {
            y_max = y_min + 4;
        }
    }

    if (out_y_min != NULL) {
        *out_y_min = y_min;
    }
    if (out_y_max != NULL) {
        *out_y_max = y_max;
    }

    return true;
}

bool temp_history_logging_enabled(void)
{
    bool enabled = false;
    aquapilot_settings_get_temp_graph_logging_enabled(&enabled);
    return enabled;
}

void temp_history_clear(void)
{
    if (s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    store_defaults();
    s_last_bucket = -1;
    save_to_sd();
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "temperature history cleared");
}
