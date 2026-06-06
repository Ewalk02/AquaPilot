#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void co2_schedule_init(void);

bool co2_schedule_clock_ready(void);
bool co2_schedule_is_injection_active(void);

/** Format "HH:MM – HH:MM" from persisted schedule. */
void co2_schedule_format_range(char *buf, size_t len);

/**
 * Format countdown line: "Stops in …", "Starts in …", or clock-sync hint.
 */
void co2_schedule_format_countdown(char *buf, size_t len);
