#include "filter_calibration.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/shelly_client.h"
#include "net/wifi_manager.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "filter_cal";

#define WAIT_POWER_TIMEOUT_MS   (120 * 1000)
#define CAL_START_SETTLE_MS     500

static volatile filter_cal_state_t s_state = FILTER_CAL_IDLE;
static volatile bool s_cancel_requested;
static volatile uint8_t s_progress_pct;
static volatile uint16_t s_current_watts;
static volatile bool s_current_watts_valid;
static volatile float s_running_average;
static volatile float s_result_baseline;
static TaskHandle_t s_task_handle;

static void finish_calibration_task(void)
{
    shelly_client_set_exclusive_filter_mode(false);
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static bool read_filter_watts(uint16_t *watts)
{
    if (watts == NULL) {
        return false;
    }

    if (!aquapilot_wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi not connected");
        return false;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
        ESP_LOGW(TAG, "filter Shelly address not configured");
        return false;
    }

    const esp_err_t err = shelly_client_get_plug_power_watts(AQUAPILOT_SHELLY_FILTER, watts);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "filter Shelly read failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

static void reset_live_readings(void)
{
    s_progress_pct = 0;
    s_current_watts = 0;
    s_current_watts_valid = false;
    s_running_average = 0.0f;
    s_result_baseline = 0.0f;
}

static void update_live_watts(uint16_t watts)
{
    s_current_watts = watts;
    s_current_watts_valid = true;
}

static void calibration_task(void *arg)
{
    (void)arg;

    shelly_client_set_exclusive_filter_mode(true);
    vTaskDelay(pdMS_TO_TICKS(CAL_START_SETTLE_MS));

    reset_live_readings();
    s_cancel_requested = false;
    s_state = FILTER_CAL_WAITING_POWER;

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
        s_state = FILTER_CAL_FAILED;
        finish_calibration_task();
        return;
    }

    if (!aquapilot_wifi_is_connected()) {
        s_state = FILTER_CAL_FAILED;
        finish_calibration_task();
        return;
    }

    aquapilot_settings_set_filter_calibrated(false);

    const TickType_t wait_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(WAIT_POWER_TIMEOUT_MS);
    bool power_detected = false;
    while (xTaskGetTickCount() < wait_deadline) {
        if (s_cancel_requested) {
            s_state = FILTER_CAL_IDLE;
            finish_calibration_task();
            return;
        }

        uint16_t watts = 0;
        if (read_filter_watts(&watts)) {
            update_live_watts(watts);
            if (watts >= FILTER_CALIBRATION_MIN_WATTS) {
                power_detected = true;
                break;
            }
        } else {
            s_current_watts_valid = false;
        }

        vTaskDelay(pdMS_TO_TICKS(FILTER_CALIBRATION_POLL_MS));
    }

    if (s_cancel_requested) {
        s_state = FILTER_CAL_IDLE;
        finish_calibration_task();
        return;
    }

    if (!power_detected) {
        s_state = FILTER_CAL_FAILED;
        finish_calibration_task();
        return;
    }

    s_state = FILTER_CAL_SAMPLING;
    uint32_t sample_sum = 0;
    uint32_t sample_count = 0;
    uint16_t watts = 0;

    for (int sample = 0; sample < FILTER_CALIBRATION_SAMPLE_COUNT; sample++) {
        if (s_cancel_requested) {
            s_state = FILTER_CAL_IDLE;
            finish_calibration_task();
            return;
        }

        watts = 0;
        if (read_filter_watts(&watts)) {
            update_live_watts(watts);
            sample_sum += watts;
            sample_count++;
            s_running_average = (float)sample_sum / (float)sample_count;
        } else {
            s_current_watts_valid = false;
        }

        s_progress_pct = (uint8_t)(((sample + 1) * 100) / FILTER_CALIBRATION_SAMPLE_COUNT);
        vTaskDelay(pdMS_TO_TICKS(FILTER_CALIBRATION_POLL_MS));
    }

    if (sample_count == 0) {
        s_state = FILTER_CAL_FAILED;
        finish_calibration_task();
        return;
    }

    const float average = (float)sample_sum / (float)sample_count;
    if (!aquapilot_settings_set_filter_baseline_watts(average)) {
        s_state = FILTER_CAL_FAILED;
        finish_calibration_task();
        return;
    }

    s_result_baseline = average;
    s_running_average = average;
    s_progress_pct = 100;
    s_state = FILTER_CAL_COMPLETE;
    ESP_LOGI(TAG, "calibration complete: %.1f W (%u samples)", average, (unsigned)sample_count);

    finish_calibration_task();
}

esp_err_t filter_calibration_init(void)
{
    ESP_LOGI(TAG, "filter calibration ready");
    return ESP_OK;
}

void filter_calibration_start(void)
{
    if (s_task_handle != NULL) {
        return;
    }

    s_state = FILTER_CAL_WAITING_POWER;
    s_cancel_requested = false;

    if (xTaskCreate(calibration_task, "filter_cal", 8192, NULL, 5, &s_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "failed to create calibration task");
        s_state = FILTER_CAL_FAILED;
        s_task_handle = NULL;
        shelly_client_set_exclusive_filter_mode(false);
    }
}

void filter_calibration_cancel(void)
{
    if (s_task_handle == NULL) {
        s_state = FILTER_CAL_IDLE;
        reset_live_readings();
        shelly_client_set_exclusive_filter_mode(false);
        return;
    }

    s_cancel_requested = true;
}

bool filter_calibration_is_active(void)
{
    return s_task_handle != NULL || s_state == FILTER_CAL_WAITING_POWER || s_state == FILTER_CAL_SAMPLING;
}

filter_cal_state_t filter_calibration_get_state(void)
{
    return s_state;
}

uint8_t filter_calibration_get_progress_pct(void)
{
    return s_progress_pct;
}

bool filter_calibration_get_current_watts(uint16_t *watts)
{
    if (watts == NULL || !s_current_watts_valid) {
        return false;
    }

    *watts = s_current_watts;
    return true;
}

bool filter_calibration_get_running_average(float *watts)
{
    if (watts == NULL || s_state != FILTER_CAL_SAMPLING || s_running_average <= 0.0f) {
        return false;
    }

    *watts = s_running_average;
    return true;
}

bool filter_calibration_get_result_baseline(float *watts)
{
    if (watts == NULL || s_state != FILTER_CAL_COMPLETE || s_result_baseline <= 0.0f) {
        return false;
    }

    *watts = s_result_baseline;
    return true;
}

const char *filter_calibration_get_message(void)
{
    switch (s_state) {
    case FILTER_CAL_IDLE:
        return "Turn on a cleaned, purged filter on the Shelly plug, then tap Start Calibration.";
    case FILTER_CAL_WAITING_POWER:
        return "Waiting for filter power... Turn on the cleaned, purged filter.";
    case FILTER_CAL_SAMPLING:
        return "Measuring filter power. Keep the filter running.";
    case FILTER_CAL_COMPLETE:
        return "Calibration complete.";
    case FILTER_CAL_FAILED:
        return "Calibration failed. Check Wi-Fi, Shelly address, and that the filter is on.";
    default:
        return "";
    }
}
