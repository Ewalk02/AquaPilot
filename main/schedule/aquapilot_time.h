#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void aquapilot_time_init(void);
void aquapilot_time_apply_settings(void);

bool aquapilot_time_is_ready(void);
void aquapilot_time_format_current(char *buf, size_t len);

bool aquapilot_time_set_manual_from_local(int year, int month, int day, int hour, int minute);
