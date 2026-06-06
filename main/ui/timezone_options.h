#pragma once

#include <stddef.h>

size_t timezone_options_count(void);
const char *timezone_options_posix(size_t index);
const char *timezone_options_label(size_t index);
const char *timezone_options_dropdown_string(void);
int timezone_options_find_index(const char *posix);
