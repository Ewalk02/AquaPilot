#pragma once

#include <stdbool.h>
#include <stdint.h>

void air_schedule_init(void);

bool air_schedule_clock_ready(void);

/** True when local time is inside the configured air pump schedule window. */
bool air_schedule_is_active(void);

/**
 * Desired air plug state: enabled, in schedule window, and CO2 injection is not active
 * unless simultaneous operation is enabled.
 */
bool air_schedule_desired_plug_on(void);

/** True when air is enabled and its schedule overlaps the CO2 schedule. */
bool air_co2_schedules_conflict(void);

/** True when minute m (0–1439) is inside [on_min, off_min) with overnight wrap. */
bool schedule_minute_in_range(int minute, int on_min, int off_min);

/** True when two schedule windows share any active minute. */
bool schedule_ranges_overlap(int on1, int off1, int on2, int off2);

/** True when two HH:MM schedules share any active minute. */
bool schedule_times_overlap(uint8_t on1_h, uint8_t on1_m, uint8_t off1_h, uint8_t off1_m, uint8_t on2_h,
                            uint8_t on2_m, uint8_t off2_h, uint8_t off2_m);
