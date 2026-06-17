#include "maintenance_tracker.h"

#include "aquapilot_time.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "storage/aquapilot_settings.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "maint_track";

static const uint8_t s_default_interval_days[MAINT_BUILTIN_COUNT] = {
    21, /* Water Change */
    7,  /* Water Sampling */
    90, /* Filter Cleaning */
    14, /* Check CO2 */
    21, /* Fill Feeder */
};

static const char *s_labels[MAINT_BUILTIN_COUNT] = {
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

static bool get_custom_item(maintenance_activity_t id, aquapilot_maint_custom_t *out)
{
    if (!maintenance_is_custom(id) || out == NULL) {
        return false;
    }

    return aquapilot_settings_get_maint_custom(maintenance_custom_slot(id), out);
}

static bool set_custom_item(maintenance_activity_t id, const aquapilot_maint_custom_t *item, bool persist_now)
{
    if (!maintenance_is_custom(id) || item == NULL) {
        return false;
    }

    if (!aquapilot_settings_update_maint_custom(maintenance_custom_slot(id), item)) {
        return false;
    }

    if (persist_now) {
        return aquapilot_settings_commit();
    }

    schedule_deferred_save();
    return true;
}

static int32_t get_next_due(maintenance_activity_t id)
{
    if (maintenance_is_custom(id)) {
        aquapilot_maint_custom_t item = {0};
        if (!get_custom_item(id, &item)) {
            return 0;
        }
        return item.next_due_epoch;
    }

    int32_t epoch = 0;
    if (!aquapilot_settings_get_maint_next_due(id, &epoch)) {
        return 0;
    }
    return epoch;
}

static bool set_next_due_epoch(maintenance_activity_t id, int32_t epoch)
{
    if (maintenance_is_custom(id)) {
        aquapilot_maint_custom_t item = {0};
        if (!get_custom_item(id, &item) || item.name[0] == '\0') {
            return false;
        }
        item.next_due_epoch = epoch;
        return set_custom_item(id, &item, false);
    }

    if (!aquapilot_settings_update_maint_next_due(id, epoch)) {
        return false;
    }

    schedule_deferred_save();
    return true;
}

static int interval_days_for(maintenance_activity_t id)
{
    if (maintenance_is_custom(id)) {
        aquapilot_maint_custom_t item = {0};
        if (get_custom_item(id, &item) && item.interval_days > 0) {
            return (int)item.interval_days;
        }
        return 1;
    }

    uint8_t days = 0;
    if (aquapilot_settings_get_maint_interval_days(id, &days) && days > 0) {
        return (int)days;
    }
    if (maintenance_is_builtin(id)) {
        return (int)s_default_interval_days[id];
    }
    return 1;
}

static int collect_active_ids(maintenance_activity_t *out, int max)
{
    int count = 0;

    for (int i = 0; i < MAINT_BUILTIN_COUNT && count < max; i++) {
        out[count++] = i;
    }

    for (int slot = 0; slot < MAINT_CUSTOM_MAX && count < max; slot++) {
        const maintenance_activity_t id = MAINT_BUILTIN_COUNT + slot;
        if (maintenance_custom_is_active(id)) {
            out[count++] = id;
        }
    }

    return count;
}

static void trim_name(char *dest, size_t dest_len, const char *src)
{
    if (dest == NULL || dest_len == 0) {
        return;
    }

    dest[0] = '\0';
    if (src == NULL) {
        return;
    }

    while (*src != '\0' && isspace((unsigned char)*src)) {
        src++;
    }

    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) {
        len--;
    }

    if (len >= dest_len) {
        len = dest_len - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

esp_err_t maintenance_tracker_init(void)
{
    ESP_LOGI(TAG, "maintenance tracker ready");
    return ESP_OK;
}

int maintenance_activity_count(void)
{
    return MAINT_BUILTIN_COUNT + (MAINT_CUSTOM_MAX - maintenance_custom_slots_available());
}

bool maintenance_custom_is_active(maintenance_activity_t id)
{
    if (!maintenance_is_custom(id)) {
        return false;
    }

    aquapilot_maint_custom_t item = {0};
    return get_custom_item(id, &item) && item.name[0] != '\0';
}

int maintenance_custom_slots_available(void)
{
    int free_slots = 0;
    for (int slot = 0; slot < MAINT_CUSTOM_MAX; slot++) {
        aquapilot_maint_custom_t item = {0};
        if (aquapilot_settings_get_maint_custom(slot, &item) && item.name[0] == '\0') {
            free_slots++;
        }
    }
    return free_slots;
}

const char *maintenance_activity_label(maintenance_activity_t id)
{
    if (maintenance_is_builtin(id)) {
        return s_labels[id];
    }

    if (maintenance_is_custom(id)) {
        static char s_custom_label[MAINT_CUSTOM_MAX][AQUAPILOT_MAINT_CUSTOM_NAME_LEN];
        const int slot = maintenance_custom_slot(id);
        if (slot < 0 || slot >= MAINT_CUSTOM_MAX) {
            return "?";
        }

        aquapilot_maint_custom_t item = {0};
        if (!get_custom_item(id, &item) || item.name[0] == '\0') {
            return "?";
        }

        strncpy(s_custom_label[slot], item.name, sizeof(s_custom_label[slot]) - 1);
        s_custom_label[slot][sizeof(s_custom_label[slot]) - 1] = '\0';
        return s_custom_label[slot];
    }

    return "?";
}

int maintenance_days_until_due(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || !aquapilot_time_is_ready()) {
        return INT32_MIN;
    }

    if (maintenance_is_custom(id) && !maintenance_custom_is_active(id)) {
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

    maintenance_activity_t ids[MAINT_ACTIVITY_COUNT];
    const int count = collect_active_ids(ids, MAINT_ACTIVITY_COUNT);

    bool any_overdue = false;
    bool any_soon = false;

    for (int i = 0; i < count; i++) {
        const int days = maintenance_days_until_due(ids[i]);
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
    const int count = collect_active_ids(order, MAINT_ACTIVITY_COUNT);

    if (aquapilot_time_is_ready()) {
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
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
            out_ids[i] = i < count ? order[i] : MAINT_WATER_CHANGE;
        }
        if (out_days != NULL) {
            out_days[i] = i < count ? maintenance_days_until_due(order[i]) : INT32_MIN;
        }
    }
}

bool maintenance_complete(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || !aquapilot_time_is_ready()) {
        return false;
    }

    if (maintenance_is_custom(id) && !maintenance_custom_is_active(id)) {
        return false;
    }

    const time_t today = today_midnight_local();
    if (today <= 0) {
        return false;
    }

    const int32_t next_due = (int32_t)(today + (time_t)interval_days_for(id) * 86400LL);
    if (!set_next_due_epoch(id, next_due)) {
        return false;
    }

    ESP_LOGI(TAG, "%s completed, next due epoch %ld", maintenance_activity_label(id), (long)next_due);
    return true;
}

bool maintenance_delay_one_week(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || !aquapilot_time_is_ready()) {
        return false;
    }

    if (maintenance_is_custom(id) && !maintenance_custom_is_active(id)) {
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

    ESP_LOGI(TAG, "%s delayed 1 week, next due epoch %ld", maintenance_activity_label(id), (long)next_due);
    return true;
}

int maintenance_interval_days(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT) {
        return 1;
    }
    return interval_days_for(id);
}

bool maintenance_set_interval_days(maintenance_activity_t id, int days)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || days < 1 || days > 255) {
        return false;
    }

    if (maintenance_is_custom(id)) {
        aquapilot_maint_custom_t item = {0};
        if (!get_custom_item(id, &item) || item.name[0] == '\0') {
            return false;
        }
        item.interval_days = (uint8_t)days;
        if (!set_custom_item(id, &item, false)) {
            return false;
        }
        ESP_LOGI(TAG, "%s interval set to %d days", item.name, days);
        return true;
    }

    if (!aquapilot_settings_update_maint_interval_days(id, (uint8_t)days)) {
        return false;
    }

    schedule_deferred_save();
    ESP_LOGI(TAG, "%s interval set to %d days", s_labels[id], days);
    return true;
}

int maintenance_add_custom(const char *name, int interval_days)
{
    if (interval_days < 1 || interval_days > 255) {
        return -1;
    }

    char trimmed[AQUAPILOT_MAINT_CUSTOM_NAME_LEN];
    trim_name(trimmed, sizeof(trimmed), name);
    if (trimmed[0] == '\0') {
        return -1;
    }

    const int slot = aquapilot_settings_find_free_maint_custom_slot();
    if (slot < 0) {
        return -1;
    }

    aquapilot_maint_custom_t item = {0};
    strncpy(item.name, trimmed, sizeof(item.name) - 1);
    item.name[sizeof(item.name) - 1] = '\0';
    item.interval_days = (uint8_t)interval_days;

    const time_t today = today_midnight_local();
    if (today > 0 && aquapilot_time_is_ready()) {
        item.next_due_epoch = (int32_t)(today + (time_t)interval_days * 86400LL);
    } else {
        item.next_due_epoch = 0;
    }

    if (!aquapilot_settings_set_maint_custom(slot, &item)) {
        return -1;
    }

    ESP_LOGI(TAG, "added custom task \"%s\" every %d days (slot %d)", item.name, interval_days, slot);
    return MAINT_BUILTIN_COUNT + slot;
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
