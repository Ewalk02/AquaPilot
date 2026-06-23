#include "air_schedule.h"

#include "schedule/aquapilot_time.h"
#include "schedule/co2_schedule.h"
#include "storage/aquapilot_settings.h"

#include <time.h>

static int minutes_of_day(uint8_t hour, uint8_t minute)
{
    return (int)hour * 60 + (int)minute;
}

static bool read_air_schedule(int *on_min, int *off_min)
{
    uint8_t on_h = 21;
    uint8_t on_m = 0;
    uint8_t off_h = 7;
    uint8_t off_m = 0;

    if (!aquapilot_settings_get_air_schedule(&on_h, &on_m, &off_h, &off_m)) {
        return false;
    }

    *on_min = minutes_of_day(on_h, on_m);
    *off_min = minutes_of_day(off_h, off_m);
    return true;
}

static bool read_co2_schedule(int *on_min, int *off_min)
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

bool schedule_minute_in_range(int minute, int on_min, int off_min)
{
    if (on_min <= off_min) {
        return minute >= on_min && minute < off_min;
    }

    return minute >= on_min || minute < off_min;
}

bool schedule_ranges_overlap(int on1, int off1, int on2, int off2)
{
    if (on1 == off1 || on2 == off2) {
        return false;
    }

    for (int minute = 0; minute < 24 * 60; minute++) {
        if (schedule_minute_in_range(minute, on1, off1) && schedule_minute_in_range(minute, on2, off2)) {
            return true;
        }
    }
    return false;
}

bool schedule_times_overlap(uint8_t on1_h, uint8_t on1_m, uint8_t off1_h, uint8_t off1_m, uint8_t on2_h,
                            uint8_t on2_m, uint8_t off2_h, uint8_t off2_m)
{
    const int on1 = minutes_of_day(on1_h, on1_m);
    const int off1 = minutes_of_day(off1_h, off1_m);
    const int on2 = minutes_of_day(on2_h, on2_m);
    const int off2 = minutes_of_day(off2_h, off2_m);
    return schedule_ranges_overlap(on1, off1, on2, off2);
}

void air_schedule_init(void)
{
}

bool air_schedule_clock_ready(void)
{
    return aquapilot_time_is_ready();
}

bool air_schedule_is_active(void)
{
    int on_min = 0;
    int off_min = 0;
    int now_min = 0;

    if (!read_air_schedule(&on_min, &off_min) || !get_local_minutes(&now_min)) {
        return false;
    }

    return schedule_minute_in_range(now_min, on_min, off_min);
}

bool air_schedule_desired_plug_on(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_air_pump_enabled(&enabled) || !enabled) {
        return false;
    }

    if (!air_schedule_clock_ready()) {
        return false;
    }

    bool simultaneous = false;
    if (aquapilot_settings_get_co2_air_simultaneous(&simultaneous) && simultaneous) {
        return air_schedule_is_active();
    }

    if (co2_schedule_is_injection_active()) {
        return false;
    }

    return air_schedule_is_active();
}

bool air_co2_schedules_conflict(void)
{
    bool simultaneous = false;
    if (aquapilot_settings_get_co2_air_simultaneous(&simultaneous) && simultaneous) {
        return false;
    }

    bool air_enabled = false;
    if (!aquapilot_settings_get_air_pump_enabled(&air_enabled) || !air_enabled) {
        return false;
    }

    bool co2_enabled = false;
    if (!aquapilot_settings_get_co2_injection_enabled(&co2_enabled) || !co2_enabled) {
        return false;
    }

    int air_on = 0;
    int air_off = 0;
    int co2_on = 0;
    int co2_off = 0;
    if (!read_air_schedule(&air_on, &air_off) || !read_co2_schedule(&co2_on, &co2_off)) {
        return false;
    }

    return schedule_ranges_overlap(air_on, air_off, co2_on, co2_off);
}
