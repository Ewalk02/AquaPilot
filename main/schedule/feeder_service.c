#include "feeder_service.h"

#include <stdio.h>
#include <time.h>

#include "aquapilot_time.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "feeder/feeder_client.h"
#include "safety/maintenance_mode.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "feeder";

#define CHECK_INTERVAL_MS     30000
#define FEED_TRIGGER_WINDOW_S 45

static volatile bool s_feeding;
static bool s_skip_next;
static int s_skip_slot = -1;
static int s_last_fed_yday = -1;
static int s_last_fed_slot = -1;
static char s_status[96] = "Disabled";

static int minutes_of_day(uint8_t hour, uint8_t minute)
{
    return (int)hour * 60 + (int)minute;
}

static int window_minutes(int start_min, int end_min)
{
    if (start_min < end_min) {
        return end_min - start_min;
    }
    return (24 * 60 - start_min) + end_min;
}

static int slot_minute(int slot, int start_min, int end_min, int times)
{
    const int window = window_minutes(start_min, end_min);
    const int offset = (window * (2 * slot + 1)) / (2 * times);
    int minute = start_min + offset;
    while (minute >= 24 * 60) {
        minute -= 24 * 60;
    }
    return minute;
}

static bool read_schedule(int *start_min, int *end_min, int *times, uint16_t *amount_seconds)
{
    uint8_t start_h = 0;
    uint8_t start_m = 0;
    uint8_t end_h = 0;
    uint8_t end_m = 0;
    uint8_t times_per_day = 0;

    if (!aquapilot_settings_get_feeder_schedule(&start_h, &start_m, &end_h, &end_m, &times_per_day, amount_seconds)) {
        return false;
    }

    *start_min = minutes_of_day(start_h, start_m);
    *end_min = minutes_of_day(end_h, end_m);
    *times = (int)times_per_day;
    return *times > 0;
}

static bool get_local_time_parts(int *yday, int *now_min, int *now_sec)
{
    if (!aquapilot_time_is_ready()) {
        return false;
    }

    const time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        return false;
    }

    *yday = local.tm_yday;
    *now_min = local.tm_hour * 60 + local.tm_min;
    *now_sec = local.tm_sec;
    return true;
}

static bool in_feeding_window(int now_min, int start_min, int end_min)
{
    if (start_min < end_min) {
        return now_min >= start_min && now_min < end_min;
    }
    return now_min >= start_min || now_min < end_min;
}

static int seconds_until_slot(int now_min, int now_sec, int slot_min)
{
    int delta_min = slot_min - now_min;
    if (delta_min < 0) {
        delta_min += 24 * 60;
    }
    return delta_min * 60 - now_sec;
}

static int next_slot_index(int now_min, int now_sec, int start_min, int end_min, int times)
{
    int best_slot = -1;
    int best_seconds = 24 * 60 * 60;

    for (int slot = 0; slot < times; slot++) {
        const int slot_min = slot_minute(slot, start_min, end_min, times);
        const int secs = seconds_until_slot(now_min, now_sec, slot_min);
        if (secs < best_seconds) {
            best_seconds = secs;
            best_slot = slot;
        }
    }

    return best_slot;
}

static int effective_next_slot(int now_min, int now_sec, int start_min, int end_min, int times)
{
    int slot = next_slot_index(now_min, now_sec, start_min, end_min, times);
    if (slot < 0) {
        return -1;
    }

    if (s_skip_next && slot == s_skip_slot) {
        int best_slot = -1;
        int best_seconds = 24 * 60 * 60;
        for (int candidate = 0; candidate < times; candidate++) {
            if (candidate == s_skip_slot) {
                continue;
            }
            const int slot_min = slot_minute(candidate, start_min, end_min, times);
            const int secs = seconds_until_slot(now_min, now_sec, slot_min);
            if (secs < best_seconds) {
                best_seconds = secs;
                best_slot = candidate;
            }
        }
        return best_slot;
    }

    return slot;
}

static void format_duration(uint32_t seconds, char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (seconds >= 24U * 3600U) {
        const unsigned hours = seconds / 3600U;
        const unsigned mins = (seconds % 3600U) / 60U;
        snprintf(buf, len, "%uh %um", hours, mins);
        return;
    }

    if (seconds >= 3600U) {
        const unsigned hours = seconds / 3600U;
        const unsigned mins = (seconds % 3600U) / 60U;
        snprintf(buf, len, "%uh %um", hours, mins);
        return;
    }

    if (seconds >= 60U) {
        snprintf(buf, len, "%um", (unsigned)(seconds / 60U));
        return;
    }

    snprintf(buf, len, "%us", (unsigned)seconds);
}

static void set_status(const char *text)
{
    if (text == NULL) {
        return;
    }
    snprintf(s_status, sizeof(s_status), "%s", text);
}

static void run_feed(uint16_t amount_seconds, const char *reason)
{
    if (s_feeding) {
        return;
    }

    s_feeding = true;
    set_status("Feeding...");
    ESP_LOGI(TAG, "%s for %u s", reason, (unsigned)amount_seconds);

    if (aquapilot_settings_has_feeder_host()) {
        if (feeder_client_feed(amount_seconds) != ESP_OK) {
            set_status("Feeder unreachable");
            s_feeding = false;
            ESP_LOGW(TAG, "feeder feed command failed");
            return;
        }
        set_status("Feed complete");
    } else {
        ESP_LOGW(TAG, "%s simulated (%u s, no feeder host configured)", reason, (unsigned)amount_seconds);
        vTaskDelay(pdMS_TO_TICKS((uint32_t)amount_seconds * 1000U));
        set_status("Feed complete (no feeder host)");
    }

    s_feeding = false;
    ESP_LOGI(TAG, "feed complete");
}

static void feed_task(void *arg)
{
    const uint16_t amount_seconds = (uint16_t)(uintptr_t)arg;
    run_feed(amount_seconds, "manual feed");
    vTaskDelete(NULL);
}

static void start_feed_async(uint16_t amount_seconds)
{
    if (s_feeding) {
        return;
    }

    if (xTaskCreate(feed_task, "feeder_feed", 3072, (void *)(uintptr_t)amount_seconds, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start feed task");
        set_status("Feed failed to start");
        return;
    }
}

static bool slot_due_now(int slot, int now_min, int now_sec, int start_min, int end_min, int times)
{
    const int slot_min = slot_minute(slot, start_min, end_min, times);
    const int slot_sec = slot_min * 60;
    const int now_total = now_min * 60 + now_sec;
    int delta = now_total - slot_sec;
    if (delta < -60) {
        delta += 24 * 60 * 60;
    }
    return delta >= 0 && delta <= FEED_TRIGGER_WINDOW_S;
}

static void maybe_run_scheduled_feed(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_feeder_enabled(&enabled) || !enabled) {
        return;
    }
    if (maintenance_mode_is_active() || s_feeding) {
        return;
    }
    if (!aquapilot_time_is_ready()) {
        return;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_seconds = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_seconds)) {
        return;
    }

    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return;
    }

    if (!in_feeding_window(now_min, start_min, end_min)) {
        return;
    }

    for (int slot = 0; slot < times; slot++) {
        if (!slot_due_now(slot, now_min, now_sec, start_min, end_min, times)) {
            continue;
        }
        if (s_last_fed_yday == yday && s_last_fed_slot == slot) {
            continue;
        }
        if (s_skip_next && slot == s_skip_slot) {
            s_skip_next = false;
            s_skip_slot = -1;
            s_last_fed_yday = yday;
            s_last_fed_slot = slot;
            set_status("Skipped scheduled feeding");
            ESP_LOGI(TAG, "skipped scheduled slot %d", slot);
            return;
        }

        s_last_fed_yday = yday;
        s_last_fed_slot = slot;
        start_feed_async(amount_seconds);
        return;
    }
}

static void automation_task(void *arg)
{
    (void)arg;

    while (true) {
        maybe_run_scheduled_feed();
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    }
}

esp_err_t feeder_service_init(void)
{
    bool enabled = false;
    if (aquapilot_settings_get_feeder_enabled(&enabled) && enabled) {
        set_status("Enabled");
    } else {
        set_status("Disabled");
    }

    if (xTaskCreate(automation_task, "feeder_auto", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create automation task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "automatic feeder service started");
    return ESP_OK;
}

bool feeder_service_is_enabled(void)
{
    bool enabled = false;
    aquapilot_settings_get_feeder_enabled(&enabled);
    return enabled;
}

bool feeder_service_is_feeding(void)
{
    return s_feeding;
}

bool feeder_service_is_ready(void)
{
    return feeder_service_is_enabled() && aquapilot_time_is_ready();
}

const char *feeder_service_status_text(void)
{
    return s_status;
}

bool feeder_service_get_times_per_day(uint8_t *times_per_day)
{
    if (times_per_day == NULL) {
        return false;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_seconds = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_seconds) || times <= 0) {
        return false;
    }

    *times_per_day = (uint8_t)times;
    return true;
}

void feeder_service_format_countdown(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (s_feeding) {
        snprintf(buf, len, "Feeding now");
        return;
    }

    bool enabled = false;
    if (!aquapilot_settings_get_feeder_enabled(&enabled) || !enabled) {
        buf[0] = '\0';
        return;
    }

    if (!aquapilot_time_is_ready()) {
        snprintf(buf, len, "Waiting for time sync...");
        return;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_seconds = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_seconds)) {
        snprintf(buf, len, "Schedule unavailable");
        return;
    }

    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        snprintf(buf, len, "Time unavailable");
        return;
    }

    const int slot = effective_next_slot(now_min, now_sec, start_min, end_min, times);
    if (slot < 0) {
        snprintf(buf, len, "No feedings scheduled");
        return;
    }

    const int slot_min = slot_minute(slot, start_min, end_min, times);
    const uint32_t secs = (uint32_t)seconds_until_slot(now_min, now_sec, slot_min);

    char duration[24];
    format_duration(secs, duration, sizeof(duration));
    snprintf(buf, len, "Next in %s", duration);
}

void feeder_service_format_schedule_preview(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_seconds = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_seconds)) {
        snprintf(buf, len, "Schedule: --");
        return;
    }

    char times_buf[128] = "";
    size_t used = 0;
    for (int slot = 0; slot < times; slot++) {
        const int minute = slot_minute(slot, start_min, end_min, times);
        const unsigned hour = (unsigned)(minute / 60);
        const unsigned min = (unsigned)(minute % 60);
        int written = 0;
        if (slot == 0) {
            written = snprintf(times_buf + used, sizeof(times_buf) - used, "%02u:%02u", hour, min);
        } else {
            written = snprintf(times_buf + used, sizeof(times_buf) - used, ", %02u:%02u", hour, min);
        }
        if (written < 0 || (size_t)written >= sizeof(times_buf) - used) {
            break;
        }
        used += (size_t)written;
    }

    snprintf(buf, len, "Feedings: %s (%us each)", times_buf, (unsigned)amount_seconds);
}

esp_err_t feeder_service_feed_now(void)
{
    if (s_feeding) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t amount_seconds = 0;
    int start_min = 0;
    int end_min = 0;
    int times = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_seconds)) {
        return ESP_FAIL;
    }

    start_feed_async(amount_seconds);
    return ESP_OK;
}

esp_err_t feeder_service_skip_next_feeding(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_feeder_enabled(&enabled) || !enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!aquapilot_time_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_seconds = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_seconds)) {
        return ESP_FAIL;
    }

    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return ESP_ERR_INVALID_STATE;
    }

    const int slot = next_slot_index(now_min, now_sec, start_min, end_min, times);
    if (slot < 0) {
        return ESP_FAIL;
    }

    s_skip_next = true;
    s_skip_slot = slot;
    set_status("Next feeding skipped");
    ESP_LOGI(TAG, "next feeding slot %d will be skipped", slot);
    return ESP_OK;
}
