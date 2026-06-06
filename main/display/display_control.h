#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

void display_control_init(lv_display_t *disp);

void display_control_apply_saved_settings(void);

void display_control_set_flip_180(bool flip);

void display_control_set_brightness(uint8_t brightness_pct);
