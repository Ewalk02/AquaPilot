#include "feeder_service.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "aquapilot_time.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feeder/feeder_client.h"
#include "feeder_amount.h"
#include "feeder_slots.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "feeder";

#define FEED_COMPLETE_BIT BIT0
#define FEED_COMPLETE_DISPLAY_S 10
#define FEED_COMPLETE_MATCH_EARLY_S 45
#define FEED_COMPLETE_MATCH_LATE_S  900
#define FEED_STUCK_TIMEOUT_US       (180LL * 1000000LL)

static EventGroupHandle_t s_feed_events;
static volatile bool s_feeding;
static int64_t s_feeding_started_us;
static int64_t s_complete_until_us;
static bool s_skip_next;
static int s_skip_slot = -1;
static char s_status[96] = "Disabled";
static int s_slot_yday = -1;
static bool s_slot_done[FEEDER_MAX_SLOTS];
static time_t s_miss_watch_epoch = 0;

static int minutes_of_day(uint8_t hour, uint8_t minute)
{
    return (int)hour * 60 + (int)minute;
}

static void reset_slot_tracking(int yday)
{
    if (yday != s_slot_yday) {
        memset(s_slot_done, 0, sizeof(s_slot_done));
        s_slot_yday = yday;
    }
}

static void mark_slot_complete(int yday, int slot)
{
    if (slot < 0 || slot >= FEEDER_MAX_SLOTS) {
        return;
    }
    reset_slot_tracking(yday);
    s_slot_done[slot] = true;
}

static bool read_schedule(int *start_min, int *end_min, int *times, uint16_t *amount_tenths);
static bool get_local_time_parts(int *yday, int *now_min, int *now_sec);

static time_t epoch_for_slot_today(int slot_min)
{
    const time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        return 0;
    }

    local.tm_hour = slot_min / 60;
    local.tm_min = slot_min % 60;
    local.tm_sec = 0;
    return mktime(&local);
}

static void ensure_miss_watch_started(void)
{
    if (s_miss_watch_epoch == 0 && aquapilot_time_is_ready()) {
        s_miss_watch_epoch = time(NULL);
    }
}

static void mark_slot_complete_from_callback(void)
{
    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_tenths = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths)) {
        return;
    }

    reset_slot_tracking(yday);

    const int now_total = now_min * 60 + now_sec;
    int best_slot = -1;
    int best_delta = INT_MAX;

    for (int slot = 0; slot < times; slot++) {
        const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
        const int slot_total = slot_min * 60;
        const int delta = now_total - slot_total;
        if (delta < -FEED_COMPLETE_MATCH_EARLY_S || delta > FEED_COMPLETE_MATCH_LATE_S) {
            continue;
        }
        if (delta < best_delta) {
            best_delta = delta;
            best_slot = slot;
        }
    }

    if (best_slot >= 0) {
        s_slot_done[best_slot] = true;
    }
}

static bool read_schedule(int *start_min, int *end_min, int *times, uint16_t *amount_tenths)
{
    uint8_t start_h = 0;
    uint8_t start_m = 0;
    uint8_t end_h = 0;
    uint8_t end_m = 0;
    uint8_t times_per_day = 0;

    if (!aquapilot_settings_get_feeder_schedule(&start_h, &start_m, &end_h, &end_m, &times_per_day, amount_tenths)) {
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
        const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
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
            const int slot_min = feeder_slot_minute(candidate, start_min, end_min, times);
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

static void mark_feed_complete_display(void)
{
    s_complete_until_us = esp_timer_get_time() + (int64_t)FEED_COMPLETE_DISPLAY_S * 1000000LL;
}

static bool feeding_flag_stuck(void)
{
    if (!s_feeding) {
        return false;
    }

    const int64_t elapsed = esp_timer_get_time() - s_feeding_started_us;
    if (elapsed < FEED_STUCK_TIMEOUT_US) {
        return false;
    }

    if (feeder_client_is_feeding()) {
        s_feeding_started_us = esp_timer_get_time();
        return false;
    }

    return true;
}

static bool try_begin_feed(void)
{
    if (s_feeding) {
        if (!feeding_flag_stuck()) {
            return false;
        }
        ESP_LOGW(TAG, "recovering stuck local feeding state");
        s_feeding = false;
    }

    s_feeding = true;
    s_feeding_started_us = esp_timer_get_time();
    s_complete_until_us = 0;
    return true;
}

static void end_feed(void)
{
    s_feeding = false;
}

static void wait_for_feed_complete(uint16_t amount_tenths)
{
    if (s_feed_events == NULL) {
        return;
    }

    const uint32_t amount_ms = feeder_amount_tenths_to_ms(amount_tenths);
    xEventGroupClearBits(s_feed_events, FEED_COMPLETE_BIT);
    const EventBits_t bits =
        xEventGroupWaitBits(s_feed_events, FEED_COMPLETE_BIT, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(amount_ms + 30000U));
    if (bits & FEED_COMPLETE_BIT) {
        return;
    }

    ESP_LOGW(TAG, "feed complete callback timeout, polling feeder status");
    const TickType_t poll_ticks = pdMS_TO_TICKS(500);
    const int max_polls = ((int)(amount_ms / 1000U) + 10) * 2;
    for (int i = 0; i < max_polls; i++) {
        feeder_client_feed_status_t status = {0};
        if (feeder_client_get_feed_status(&status) && status.valid) {
            char progress[96];
            feeder_client_format_feed_status(&status, progress, sizeof(progress));
            set_status(progress);
            if (!status.feeding) {
                set_status("Feed complete");
                mark_feed_complete_display();
                return;
            }
        }
        vTaskDelay(poll_ticks);
    }
}

static void run_feed(uint16_t amount_tenths, const char *reason)
{
    set_status("Feeding...");
    ESP_LOGI(TAG, "%s for %.1f s", reason, (double)feeder_amount_tenths_to_seconds(amount_tenths));

    if (aquapilot_settings_has_feeder_host()) {
        if (feeder_client_feed(amount_tenths) != ESP_OK) {
            set_status("Feeder unreachable");
            end_feed();
            ESP_LOGW(TAG, "feeder feed command failed");
            return;
        }

        wait_for_feed_complete(amount_tenths);
        if (strncmp(s_status, "Feeding", 7) == 0) {
            set_status("Feed complete");
            mark_feed_complete_display();
        }
    } else {
        ESP_LOGW(TAG, "%s simulated (%.1f s, no feeder host configured)", reason,
                 (double)feeder_amount_tenths_to_seconds(amount_tenths));
        vTaskDelay(pdMS_TO_TICKS(feeder_amount_tenths_to_ms(amount_tenths)));
        set_status("Feed complete (no feeder host)");
        mark_feed_complete_display();
    }

    end_feed();
    ESP_LOGI(TAG, "feed complete");
}

static void feed_task(void *arg)
{
    const uint16_t amount_tenths = (uint16_t)(uintptr_t)arg;
    run_feed(amount_tenths, "manual feed");
    vTaskDelete(NULL);
}

static esp_err_t start_feed_async(uint16_t amount_tenths)
{
    if (!try_begin_feed()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(feed_task, "feeder_feed", 6144, (void *)(uintptr_t)amount_tenths, 4, NULL) != pdPASS) {
        end_feed();
        ESP_LOGE(TAG, "failed to start feed task");
        set_status("Feed failed to start");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t feeder_service_init(void)
{
    s_feed_events = xEventGroupCreate();
    if (s_feed_events == NULL) {
        ESP_LOGE(TAG, "failed to create feed event group");
        return ESP_FAIL;
    }

    bool enabled = false;
    if (aquapilot_settings_get_feeder_enabled(&enabled) && enabled) {
        set_status("Enabled");
    } else {
        set_status("Disabled");
    }

    ensure_miss_watch_started();

    ESP_LOGI(TAG, "feeder service started (schedule runs on feeder device)");
    return ESP_OK;
}

void feeder_service_on_feed_complete(uint16_t amount_tenths, uint32_t steps)
{
    (void)amount_tenths;
    char buf[96];
    snprintf(buf, sizeof(buf), "Feed complete (%u steps)", (unsigned)steps);
    set_status(buf);
    mark_feed_complete_display();
    mark_slot_complete_from_callback();
    if (s_feed_events != NULL) {
        xEventGroupSetBits(s_feed_events, FEED_COMPLETE_BIT);
    }
    ESP_LOGI(TAG, "feeder reported complete (%u steps)", (unsigned)steps);
}

bool feeder_service_show_feed_complete(void)
{
    if (s_complete_until_us <= 0) {
        return false;
    }
    if (esp_timer_get_time() >= s_complete_until_us) {
        s_complete_until_us = 0;
        return false;
    }
    return true;
}

bool feeder_service_is_feeding(void)
{
    if (s_feeding && feeding_flag_stuck()) {
        ESP_LOGW(TAG, "clearing stuck feeding flag while idle");
        s_feeding = false;
    }
    return s_feeding || feeder_client_is_feeding();
}

bool feeder_service_is_feed_missed(void)
{
    if (feeder_service_is_feeding()) {
        return false;
    }

    bool enabled = false;
    if (!aquapilot_settings_get_feeder_enabled(&enabled) || !enabled) {
        return false;
    }
    if (!aquapilot_time_is_ready() || !aquapilot_settings_has_feeder_host()) {
        return false;
    }

    ensure_miss_watch_started();
    if (s_miss_watch_epoch == 0) {
        return false;
    }

    int start_min = 0;
    int end_min = 0;
    int times = 0;
    uint16_t amount_tenths = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths)) {
        return false;
    }

    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return false;
    }

    reset_slot_tracking(yday);

    const time_t now = time(NULL);
    for (int slot = 0; slot < times; slot++) {
        const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
        const time_t slot_epoch = epoch_for_slot_today(slot_min);
        if (slot_epoch == 0) {
            continue;
        }
        if (slot_epoch < s_miss_watch_epoch) {
            continue;
        }
        if (now <= slot_epoch + FEEDER_MISSED_GRACE_S) {
            continue;
        }
        if (!s_slot_done[slot]) {
            return true;
        }
    }

    return false;
}

bool feeder_service_is_enabled(void)
{
    bool enabled = false;
    aquapilot_settings_get_feeder_enabled(&enabled);
    return enabled;
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
    uint16_t amount_tenths = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths) || times <= 0) {
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
        snprintf(buf, len, "%s", s_status);
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
    uint16_t amount_tenths = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths)) {
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

    const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
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
    uint16_t amount_tenths = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths)) {
        snprintf(buf, len, "Schedule: --");
        return;
    }

    char times_buf[128] = "";
    size_t used = 0;
    for (int slot = 0; slot < times; slot++) {
        const int minute = feeder_slot_minute(slot, start_min, end_min, times);
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

    snprintf(buf, len, "Feedings: %s (%.1fs each)", times_buf, (double)feeder_amount_tenths_to_seconds(amount_tenths));
}

esp_err_t feeder_service_feed_now(void)
{
    uint16_t amount_tenths = 0;
    int start_min = 0;
    int end_min = 0;
    int times = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths)) {
        return ESP_FAIL;
    }

    return start_feed_async(amount_tenths);
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
    uint16_t amount_tenths = 0;
    if (!read_schedule(&start_min, &end_min, &times, &amount_tenths)) {
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

    if (aquapilot_settings_has_feeder_host()) {
        if (feeder_client_skip_next() != ESP_OK) {
            set_status("Feeder unreachable");
            return ESP_FAIL;
        }
    }

    s_skip_next = true;
    s_skip_slot = slot;
    mark_slot_complete(yday, slot);
    set_status("Next feeding skipped");
    ESP_LOGI(TAG, "next feeding slot %d will be skipped", slot);
    return ESP_OK;
}
