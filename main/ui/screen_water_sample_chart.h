#pragma once

#include "lvgl.h"
#include "storage/water_sample_history.h"

void screen_water_sample_chart_create(void);
void screen_water_sample_chart_show(water_sample_metric_t metric);

/** Open chart and return to return_screen on Back (NULL = water sampling screen). */
void screen_water_sample_chart_show_to(water_sample_metric_t metric, lv_obj_t *return_screen);
