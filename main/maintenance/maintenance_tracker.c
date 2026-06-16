#include "maintenance_tracker.h"

#include "aquapilot_time.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "storage/aquapilot_settings.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "maint_track";

static const int s_interval_days[MAINT_ACTIVITY_COUNT] = {
    21, /* Water Change */
    7,  /* Water Sampling */
    90, /* Filter Cleaning */
    14, /* Check CO2 */
    21, /* Fill Feeder */
};

static const char *s_labels[MAINT_ACTIVITY_COUNT] = {
    "Water Change",
    "Water Sampling",
    "Filter Cleaning",
    "Check CO2",
    "Fill Feeder",
};

static esp_timer_handle_t s_save_timer;

static void deferred_save_cb(void *arg)
{
    (void)arg;
    (void)aquapilot_settings_commit();
}

static void schedule_deferred_save(void)
{
    if (s_save_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = deferred_save_cb,
            .name = "maint_save",
        };
        if (esp_timer_create(&args, &s_save_timer) != ESP_OK) {
            (void)aquapilot_settings_commit();
            return;
        }
    }

    esp_timer_stop(s_save_timer);
    (void)esp_timer_start_once(s_save_timer, 200000);
}

static bool set_next_due(maintenance_activity_t id, int32_t epoch, bool persist_now)
{
    if (!aquapilot_settings_update_maint_next_due((int)id, epoch)) {
        return false;
    }

    if (persist_now) {
        return aquapilot_settings_commit();
    }

    schedule_deferred_save();
    return true;
}

static time_t today_midnight_local(void)
{
    const time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        return 0;
    }

    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    return mktime(&local);
}

static int32_t get_next_due(maintenance_activity_t id)
{
    int32_t epoch = 0;
    if (!aquapilot_settings_get_maint_next_due((int)id, &epoch)) {
        return 0;
    }
    return epoch;
}

static bool set_next_due_epoch(maintenance_activity_t id, int32_t epoch)
{
    return set_next_due(id, epoch, false);
}

esp_err_t maintenance_tracker_init(void)
{
    ESP_LOGI(TAG, "maintenance tracker ready");
    return ESP_OK;
}

int maintenance_activity_count(void)
{
    return MAINT_ACTIVITY_COUNT;
}

const char *maintenance_activity_label(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT) {
        return "?";
    }
    return s_labels[id];
}

int maintenance_days_until_due(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || !aquapilot_time_is_ready()) {
        return INT32_MIN;
    }

    const int32_t next_due = get_next_due(id);
    if (next_due <= 0) {
        return -1;
    }

    const time_t today = today_midnight_local();
    if (today <= 0) {
        return INT32_MIN;
    }

    const int64_t diff_sec = (int64_t)next_due - (int64_t)today;
    return (int)(diff_sec / 86400LL);
}

maintenance_tile_severity_t maintenance_tile_severity(void)
{
    if (!aquapilot_time_is_ready()) {
        return MAINT_TILE_SEVERITY_UNKNOWN;
    }

    bool any_overdue = false;
    bool any_soon = false;

    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        const int days = maintenance_days_until_due((maintenance_activity_t)i);
        if (days < 0) {
            any_overdue = true;
        } else if (days <= 2) {
            any_soon = true;
        }
    }

    if (any_overdue) {
        return MAINT_TILE_SEVERITY_RED;
    }
    if (any_soon) {
        return MAINT_TILE_SEVERITY_YELLOW;
    }
    return MAINT_TILE_SEVERITY_GREEN;
}

static int compare_urgency(const void *a, const void *b)
{
    const maintenance_activity_t id_a = *(const maintenance_activity_t *)a;
    const maintenance_activity_t id_b = *(const maintenance_activity_t *)b;
    const int days_a = maintenance_days_until_due(id_a);
    const int days_b = maintenance_days_until_due(id_b);

    if (days_a != days_b) {
        return (days_a < days_b) ? -1 : 1;
    }
    return (id_a < id_b) ? -1 : (id_a > id_b) ? 1 : 0;
}

void maintenance_get_top3(maintenance_activity_t out_ids[3], int out_days[3])
{
    maintenance_activity_t order[MAINT_ACTIVITY_COUNT];
    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        order[i] = (maintenance_activity_t)i;
    }

    if (aquapilot_time_is_ready()) {
        for (int i = 0; i < MAINT_ACTIVITY_COUNT - 1; i++) {
            for (int j = i + 1; j < MAINT_ACTIVITY_COUNT; j++) {
                if (compare_urgency(&order[i], &order[j]) > 0) {
                    const maintenance_activity_t tmp = order[i];
                    order[i] = order[j];
                    order[j] = tmp;
                }
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        if (out_ids != NULL) {
            out_ids[i] = order[i];
        }
        if (out_days != NULL) {
            out_days[i] = maintenance_days_until_due(order[i]);
        }
    }
}

bool maintenance_complete(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || !aquapilot_time_is_ready()) {
        return false;
    }

    const time_t today = today_midnight_local();
    if (today <= 0) {
        return false;
    }

    const int32_t next_due = (int32_t)(today + (time_t)s_interval_days[id] * 86400LL);
    if (!set_next_due_epoch(id, next_due)) {
        return false;
    }

    ESP_LOGI(TAG, "%s completed, next due epoch %ld", s_labels[id], (long)next_due);
    return true;
}

bool maintenance_delay_one_week(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || !aquapilot_time_is_ready()) {
        return false;
    }

    const time_t today = today_midnight_local();
    if (today <= 0) {
        return false;
    }

    int32_t current = get_next_due(id);
    if (current <= 0) {
        current = (int32_t)today;
    }

    time_t base = (time_t)current;
    if (base < today) {
        base = today;
    }

    const int32_t next_due = (int32_t)(base + 7LL * 86400LL);
    if (!set_next_due_epoch(id, next_due)) {
        return false;
    }

    ESP_LOGI(TAG, "%s delayed 1 week, next due epoch %ld", s_labels[id], (long)next_due);
    return true;
}

void maintenance_format_due_text(maintenance_activity_t id, char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    const int days = maintenance_days_until_due(id);
    if (days == INT32_MIN) {
        snprintf(buf, len, "--");
        return;
    }
    if (days < 0) {
        snprintf(buf, len, "Overdue");
        return;
    }
    if (days == 0) {
        snprintf(buf, len, "Due today");
        return;
    }
    if (days == 1) {
        snprintf(buf, len, "1d");
        return;
    }
    snprintf(buf, len, "%dd", days);
}
