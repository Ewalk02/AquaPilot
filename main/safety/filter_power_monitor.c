#include "filter_power_monitor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/shelly_client.h"
#include "net/wifi_manager.h"
#include "safety/filter_calibration.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "filter_power";

#define MONITOR_INTERVAL_MS  2000
#define ALARM_DELAY_US       (60LL * 1000000LL)
#define READ_FAIL_ZERO_COUNT 3

static volatile bool s_alarm_active;
static volatile bool s_watts_valid;
static volatile bool s_plug_online;
static volatile bool s_filter_on;
static volatile uint16_t s_cached_watts;
static int64_t s_alarm_condition_since_us;
static uint8_t s_high_watts_streak;
static uint8_t s_low_watts_streak;
static uint8_t s_read_fail_streak;

#define FILTER_ON_DEBOUNCE  2
#define FILTER_OFF_DEBOUNCE 2

static void set_filter_off(void)
{
    s_filter_on = false;
}

static void invalidate_reading(void)
{
    s_watts_valid = false;
    s_read_fail_streak = 0;
    s_high_watts_streak = 0;
    s_low_watts_streak = 0;
    set_filter_off();
}

static void refresh_watts(void)
{
    if (filter_calibration_is_active()) {
        return;
    }

    if (!aquapilot_wifi_is_connected()) {
        s_plug_online = false;
        return;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
        s_plug_online = false;
        s_watts_valid = false;
        s_read_fail_streak = 0;
        invalidate_reading();
        return;
    }

    uint16_t watts = 0;
    if (shelly_client_get_plug_power_watts(AQUAPILOT_SHELLY_FILTER, &watts) == ESP_OK) {
        s_plug_online = true;
        s_read_fail_streak = 0;

        if (watts <= FILTER_MIN_RUNNING_WATTS) {
            s_high_watts_streak = 0;
            if (++s_low_watts_streak >= FILTER_OFF_DEBOUNCE) {
                s_cached_watts = watts;
                s_watts_valid = true;
                set_filter_off();
                ESP_LOGI(TAG, "filter watts %u", (unsigned)watts);
            }
            return;
        }

        s_low_watts_streak = 0;
        s_high_watts_streak++;
        if (s_high_watts_streak >= FILTER_ON_DEBOUNCE) {
            s_cached_watts = watts;
            s_watts_valid = true;
            s_filter_on = true;
            ESP_LOGI(TAG, "filter watts %u", (unsigned)watts);
        }
        return;
    }

    s_plug_online = false;
    if (++s_read_fail_streak >= READ_FAIL_ZERO_COUNT) {
        s_cached_watts = 0;
        s_watts_valid = true;
        set_filter_off();
    }
}

static bool compute_band_edges(float baseline, float *green_lo, float *green_hi, float *yellow_lo, float *yellow_hi,
                               float *red_lo, float *red_hi)
{
    uint8_t green_pct = 0;
    uint8_t yellow_pct = 0;
    uint8_t red_pct = 0;
    if (!aquapilot_settings_get_filter_bands(&green_pct, &yellow_pct, &red_pct, NULL)) {
        return false;
    }

    *green_lo = baseline * (100.0f - (float)green_pct) / 100.0f;
    *green_hi = baseline * (100.0f + (float)green_pct) / 100.0f;
    *yellow_lo = baseline * (100.0f - (float)yellow_pct) / 100.0f;
    *yellow_hi = baseline * (100.0f + (float)yellow_pct) / 100.0f;
    *red_lo = baseline * (100.0f - (float)red_pct) / 100.0f;
    *red_hi = baseline * (100.0f + (float)red_pct) / 100.0f;
    return true;
}

filter_power_zone_t filter_power_monitor_get_zone(void)
{
    if (!s_watts_valid) {
        return FILTER_POWER_ZONE_UNKNOWN;
    }

    if (s_cached_watts <= FILTER_MIN_RUNNING_WATTS) {
        return FILTER_POWER_ZONE_OFF;
    }

    float baseline = 0.0f;
    if (!aquapilot_settings_get_filter_baseline_watts(&baseline)) {
        return FILTER_POWER_ZONE_UNKNOWN;
    }

    float green_lo = 0.0f;
    float green_hi = 0.0f;
    float yellow_lo = 0.0f;
    float yellow_hi = 0.0f;
    float red_lo = 0.0f;
    float red_hi = 0.0f;
    if (!compute_band_edges(baseline, &green_lo, &green_hi, &yellow_lo, &yellow_hi, &red_lo, &red_hi)) {
        return FILTER_POWER_ZONE_UNKNOWN;
    }

    const float watts = (float)s_cached_watts;
    if (watts >= green_lo && watts <= green_hi) {
        return FILTER_POWER_ZONE_GREEN;
    }
    if ((watts >= yellow_lo && watts < green_lo) || (watts > green_hi && watts <= yellow_hi)) {
        return FILTER_POWER_ZONE_YELLOW;
    }
    return FILTER_POWER_ZONE_RED;
}

static bool power_is_abnormal(void)
{
    const filter_power_zone_t zone = filter_power_monitor_get_zone();
    return zone == FILTER_POWER_ZONE_YELLOW || zone == FILTER_POWER_ZONE_RED;
}

static bool evaluate_alarm(void)
{
    if (!power_is_abnormal()) {
        s_alarm_condition_since_us = 0;
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_alarm_condition_since_us == 0) {
        s_alarm_condition_since_us = now_us;
        return false;
    }

    return (now_us - s_alarm_condition_since_us) >= ALARM_DELAY_US;
}

static void monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        refresh_watts();
        s_alarm_active = evaluate_alarm();
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t filter_power_monitor_init(void)
{
    if (xTaskCreate(monitor_task, "filter_power", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "filter power monitor started");
    return ESP_OK;
}

bool filter_power_monitor_plug_is_online(void)
{
    return s_plug_online;
}

bool filter_power_monitor_get_watts(uint16_t *watts)
{
    if (watts == NULL || !s_watts_valid) {
        return false;
    }

    *watts = s_cached_watts;
    return true;
}

bool filter_power_monitor_is_filter_on(void)
{
    return s_watts_valid && s_filter_on;
}

bool filter_power_monitor_is_calibrated(void)
{
    float baseline = 0.0f;
    return aquapilot_settings_get_filter_baseline_watts(&baseline);
}

bool filter_power_monitor_get_baseline_watts(float *watts)
{
    return aquapilot_settings_get_filter_baseline_watts(watts);
}

bool filter_power_monitor_is_normal(void)
{
    return filter_power_monitor_get_zone() == FILTER_POWER_ZONE_GREEN;
}

bool filter_power_monitor_alarm_active(void)
{
    return s_alarm_active;
}

bool filter_power_monitor_get_gauge_range(float *min_watts, float *max_watts)
{
    if (min_watts == NULL || max_watts == NULL) {
        return false;
    }

    float baseline = 0.0f;
    if (!aquapilot_settings_get_filter_baseline_watts(&baseline)) {
        return false;
    }

    uint8_t cutoff_pct = 0;
    if (!aquapilot_settings_get_filter_bands(NULL, NULL, NULL, &cutoff_pct)) {
        return false;
    }

    *min_watts = baseline * (100.0f - (float)cutoff_pct) / 100.0f;
    *max_watts = baseline * (100.0f + (float)cutoff_pct) / 100.0f;
    if (*min_watts < 0.0f) {
        *min_watts = 0.0f;
    }
    if (*max_watts <= *min_watts) {
        *max_watts = *min_watts + 1.0f;
    }
    return true;
}

bool filter_power_monitor_get_gauge_max_watts(float *max_watts)
{
    float min_watts = 0.0f;
    return filter_power_monitor_get_gauge_range(&min_watts, max_watts);
}
