#include "co2_schedule.h"

#include <stdio.h>
#include <time.h>

#include "aquapilot_time.h"
#include "storage/aquapilot_settings.h"

static int minutes_of_day(uint8_t hour, uint8_t minute)
{
    return (int)hour * 60 + (int)minute;
}

static bool read_schedule(int *on_min, int *off_min)
{
    uint8_t on_h = 7;
    uint8_t on_m = 0;
    uint8_t off_h = 21;
    uint8_t off_m = 0;

    if (!aquapilot_settings_get_co2_schedule(&on_h, &on_m, &off_h, &off_m)) {
        return false;
    }

    *on_min = minutes_of_day(on_h, on_m);
    *off_min = minutes_of_day(off_h, off_m);
    return true;
}

static bool get_local_minutes(int *out_min)
{
    if (!aquapilot_time_is_ready()) {
        return false;
    }

    time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        return false;
    }

    *out_min = local.tm_hour * 60 + local.tm_min;
    return true;
}

static uint32_t seconds_until_minute(int target_min, int now_min)
{
    int delta_min = target_min - now_min;
    if (delta_min <= 0) {
        delta_min += 24 * 60;
    }
    return (uint32_t)delta_min * 60U;
}

static uint32_t seconds_until_off(int on_min, int off_min, int now_min)
{
    if (on_min <= off_min) {
        return seconds_until_minute(off_min, now_min);
    }

    if (now_min < off_min) {
        return seconds_until_minute(off_min, now_min);
    }

    return seconds_until_minute(off_min + 24 * 60, now_min);
}

static uint32_t seconds_until_on(int on_min, int off_min, int now_min)
{
    if (on_min <= off_min) {
        if (now_min < on_min) {
            return seconds_until_minute(on_min, now_min);
        }
        return seconds_until_minute(on_min + 24 * 60, now_min);
    }

    return seconds_until_minute(on_min, now_min);
}

static void format_duration(uint32_t seconds, char *buf, size_t len)
{
    const uint32_t hours = seconds / 3600U;
    const uint32_t minutes = (seconds % 3600U) / 60U;

    if (hours > 0U) {
        snprintf(buf, len, "%uh %02um", (unsigned)hours, (unsigned)minutes);
    } else if (minutes > 0U) {
        snprintf(buf, len, "%u min", (unsigned)minutes);
    } else {
        snprintf(buf, len, "< 1 min");
    }
}

void co2_schedule_init(void)
{
}

bool co2_schedule_clock_ready(void)
{
    return aquapilot_time_is_ready();
}

bool co2_schedule_is_injection_active(void)
{
    int on_min = 0;
    int off_min = 0;
    int now_min = 0;

    if (!read_schedule(&on_min, &off_min) || !get_local_minutes(&now_min)) {
        return false;
    }

    if (on_min <= off_min) {
        return now_min >= on_min && now_min < off_min;
    }

    return now_min >= on_min || now_min < off_min;
}

void co2_schedule_format_range(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    uint8_t on_h = 7;
    uint8_t on_m = 0;
    uint8_t off_h = 21;
    uint8_t off_m = 0;
    aquapilot_settings_get_co2_schedule(&on_h, &on_m, &off_h, &off_m);
    snprintf(buf, len, "%02u:%02u - %02u:%02u", (unsigned)on_h, (unsigned)on_m, (unsigned)off_h, (unsigned)off_m);
}

void co2_schedule_format_countdown(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (!co2_schedule_clock_ready()) {
        snprintf(buf, len, "Waiting for time sync...");
        return;
    }

    int on_min = 0;
    int off_min = 0;
    int now_min = 0;
    if (!read_schedule(&on_min, &off_min) || !get_local_minutes(&now_min)) {
        snprintf(buf, len, "Schedule unavailable");
        return;
    }

    char duration[24];
    if (co2_schedule_is_injection_active()) {
        format_duration(seconds_until_off(on_min, off_min, now_min), duration, sizeof(duration));
        snprintf(buf, len, "Stops in %s", duration);
    } else {
        format_duration(seconds_until_on(on_min, off_min, now_min), duration, sizeof(duration));
        snprintf(buf, len, "Starts in %s", duration);
    }
}
