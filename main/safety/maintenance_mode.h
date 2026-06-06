#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t maintenance_mode_init(void);

bool maintenance_mode_is_active(void);
bool maintenance_mode_sequence_running(void);
const char *maintenance_mode_status_text(void);

/** Persist setting and run the staged shutdown/restore sequence. */
esp_err_t maintenance_mode_apply(bool enabled);
