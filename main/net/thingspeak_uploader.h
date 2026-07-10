#pragma once

#include "esp_err.h"

esp_err_t thingspeak_uploader_init(void);

const char *thingspeak_uploader_last_status_text(void);

/** Called when SNTP steps the clock backward; forces the next upload cycle. */
void thingspeak_uploader_on_clock_step(void);
