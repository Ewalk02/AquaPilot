#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
    lv_timer_t *update_timer;
} screen_main_t;

/**
 * @brief Build and load the main dashboard screen.
 */
screen_main_t screen_main_create(void);
