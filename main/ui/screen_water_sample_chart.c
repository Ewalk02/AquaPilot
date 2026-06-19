#include "screen_water_sample_chart.h"
#include "storage/water_sample_history.h"

#include "screen_water_sampling.h"

#include "lvgl.h"
#include "ui_buttons.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define SCREEN_BG_COLOR 0x0D1117
#define PANEL_BG_COLOR  0x161B22
#define BORDER_COLOR    0x30363D
#define TITLE_COLOR     0xE6EDF3
#define LABEL_COLOR     0x8B949E
#define CHART_BG        0x0D1117
#define CHART_BORDER    0x30363D
#define CHART_GRID      0x21262D
#define SERIES_COLOR    0x58A6FF
#define Y_AXIS_WIDTH    40

static lv_obj_t *s_screen;
static lv_obj_t *s_return_screen;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;
static lv_obj_t *s_y_max_label;
static lv_obj_t *s_y_mid_label;
static lv_obj_t *s_y_min_label;
static lv_obj_t *s_x_left_label;
static lv_obj_t *s_x_mid_label;
static lv_obj_t *s_x_right_label;
static lv_obj_t *s_empty_label;
static water_sample_metric_t s_metric;

static void apply_screen_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
}

static lv_obj_t *create_axis_label(lv_obj_t *parent, lv_text_align_t align)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl, align, 0);
    return lbl;
}

static void format_date_short(int32_t epoch, char *buf, size_t len)
{
    const time_t t = (time_t)epoch;
    struct tm local = {0};
    if (localtime_r(&t, &local) == NULL) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%02d/%02d", local.tm_mon + 1, local.tm_mday);
}

static void format_y_label(char *buf, size_t len, water_sample_metric_t metric, float value)
{
    if (metric == WATER_METRIC_PH || metric == WATER_METRIC_KH || metric == WATER_METRIC_GH) {
        snprintf(buf, len, "%.1f", value);
        return;
    }

    if (metric == WATER_METRIC_AMMONIA || metric == WATER_METRIC_NITRITE) {
        if (value < 1.0f) {
            snprintf(buf, len, "%.2f", value);
        } else if (value < 10.0f) {
            snprintf(buf, len, "%.1f", value);
        } else {
            snprintf(buf, len, "%.0f", value);
        }
        return;
    }

    snprintf(buf, len, "%.0f", value);
}

static float nice_ppm_ceil(float value)
{
    static const float steps[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f};

    if (value <= 0.0f) {
        return 1.0f;
    }

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (value <= steps[i]) {
            return steps[i];
        }
    }

    return ceilf(value);
}

static void compute_y_range(water_sample_metric_t metric, float data_min, float data_max, float *y_min,
                            float *y_max)
{
    if (metric == WATER_METRIC_PH) {
        float min_v = data_min;
        float max_v = data_max;
        if (min_v > 0.0f) {
            min_v = 0.0f;
        }
        if (max_v < 14.0f) {
            max_v = fmaxf(max_v + 0.5f, 7.0f);
        }
        *y_min = min_v;
        *y_max = fminf(max_v, 14.0f);
        return;
    }

    if (metric == WATER_METRIC_AMMONIA || metric == WATER_METRIC_NITRITE) {
        float max_v = fmaxf(data_max, 0.25f);
        max_v = nice_ppm_ceil(max_v * 1.1f);
        *y_min = 0.0f;
        *y_max = max_v;
        return;
    }

    const float pad = fmaxf(0.5f, (data_max - data_min) * 0.15f);
    float min_v = fmaxf(0.0f, data_min - pad);
    float max_v = data_max + pad;
    if (max_v <= min_v) {
        max_v = min_v + 1.0f;
    }
    *y_min = min_v;
    *y_max = max_v;
}

static void compute_chart_x_range(const int32_t *epochs, uint16_t count, int64_t window_start, int32_t *x_min,
                                  int32_t *x_max)
{
    int32_t x_lo = INT32_MAX;
    int32_t x_hi = INT32_MIN;

    for (uint16_t i = 0; i < count; i++) {
        const int32_t x = (int32_t)((int64_t)epochs[i] - window_start);
        if (x < x_lo) {
            x_lo = x;
        }
        if (x > x_hi) {
            x_hi = x;
        }
    }

    int32_t pad = (x_hi - x_lo) / 8;
    if (pad < (int32_t)(WATER_SAMPLE_WINDOW_SEC / 16)) {
        pad = (int32_t)(WATER_SAMPLE_WINDOW_SEC / 16);
    }

    *x_min = x_lo - pad;
    *x_max = x_hi + pad;
    if (*x_min < 0) {
        *x_min = 0;
    }
    if (*x_max > (int32_t)WATER_SAMPLE_WINDOW_SEC) {
        *x_max = (int32_t)WATER_SAMPLE_WINDOW_SEC;
    }
    if (*x_max <= *x_min) {
        *x_max = *x_min + pad * 2;
    }
}

static void refresh_chart(void)
{
    int32_t epochs[WATER_SAMPLE_MAX_RECORDS];
    float values[WATER_SAMPLE_MAX_RECORDS];
    uint16_t count = 0;

    if (!water_sample_history_get_metric_series(s_metric, epochs, values, WATER_SAMPLE_MAX_RECORDS, &count) ||
        count == 0 || s_chart == NULL || s_series == NULL) {
        if (s_empty_label != NULL) {
            lv_obj_clear_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_chart != NULL) {
            lv_chart_set_point_count(s_chart, 1);
            lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_X, 0, 1);
            lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
            lv_chart_set_series_value_by_id2(s_chart, s_series, 0, LV_CHART_POINT_NONE, LV_CHART_POINT_NONE);
        }
        if (s_y_min_label != NULL) {
            lv_label_set_text(s_y_min_label, "0");
        }
        if (s_y_mid_label != NULL) {
            lv_label_set_text(s_y_mid_label, "0");
        }
        if (s_y_max_label != NULL) {
            lv_label_set_text(s_y_max_label, "0");
        }
        return;
    }

    if (s_empty_label != NULL) {
        lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_chart_set_point_count(s_chart, count);

    float y_min = values[0];
    float y_max = values[0];
    for (uint16_t i = 0; i < count; i++) {
        if (values[i] < y_min) {
            y_min = values[i];
        }
        if (values[i] > y_max) {
            y_max = values[i];
        }
    }

    compute_y_range(s_metric, y_min, y_max, &y_min, &y_max);

    const int32_t y_min_i = (int32_t)lroundf(y_min * 100.0f);
    int32_t y_max_i = (int32_t)lroundf(y_max * 100.0f);
    if (y_max_i <= y_min_i) {
        y_max_i = y_min_i + 100;
    }

    const time_t now = time(NULL);
    const int64_t window_start = (int64_t)now - WATER_SAMPLE_WINDOW_SEC;
    int32_t x_min = 0;
    int32_t x_max = (int32_t)WATER_SAMPLE_WINDOW_SEC;
    compute_chart_x_range(epochs, count, window_start, &x_min, &x_max);

    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_X, x_min, x_max);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, y_min_i, y_max_i);

    for (uint16_t i = 0; i < count; i++) {
        const int32_t y_val = (int32_t)lroundf(values[i] * 100.0f);
        int32_t x_val = (int32_t)((int64_t)epochs[i] - window_start);
        if (x_val < x_min) {
            x_val = x_min;
        }
        if (x_val > x_max) {
            x_val = x_max;
        }
        lv_chart_set_series_value_by_id2(s_chart, s_series, i, x_val, y_val);
    }

    const uint32_t point_cnt = lv_chart_get_point_count(s_chart);
    for (uint32_t i = count; i < point_cnt; i++) {
        lv_chart_set_series_value_by_id2(s_chart, s_series, i, LV_CHART_POINT_NONE, LV_CHART_POINT_NONE);
    }

    lv_chart_refresh(s_chart);

    char buf[16];
    if (s_y_min_label != NULL) {
        format_y_label(buf, sizeof(buf), s_metric, y_min);
        lv_label_set_text(s_y_min_label, buf);
    }
    if (s_y_mid_label != NULL) {
        format_y_label(buf, sizeof(buf), s_metric, (y_min + y_max) * 0.5f);
        lv_label_set_text(s_y_mid_label, buf);
    }
    if (s_y_max_label != NULL) {
        format_y_label(buf, sizeof(buf), s_metric, y_max);
        lv_label_set_text(s_y_max_label, buf);
    }

    if (s_x_left_label != NULL) {
        format_date_short(epochs[0], buf, sizeof(buf));
        lv_label_set_text(s_x_left_label, buf);
    }
    if (s_x_right_label != NULL) {
        format_date_short(epochs[count - 1], buf, sizeof(buf));
        lv_label_set_text(s_x_right_label, buf);
    }
    if (s_x_mid_label != NULL && count > 1) {
        format_date_short(epochs[count / 2], buf, sizeof(buf));
        lv_label_set_text(s_x_mid_label, buf);
    } else if (s_x_mid_label != NULL) {
        lv_label_set_text(s_x_mid_label, "");
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_return_screen != NULL) {
        lv_screen_load(s_return_screen);
        s_return_screen = NULL;
        return;
    }

    screen_water_sampling_show();
}

void screen_water_sample_chart_create(void)
{
    s_screen = lv_obj_create(NULL);
    apply_screen_style(s_screen);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_screen, 12, 0);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_user_data(title, (void *)"title");

    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PANEL_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chart_row = lv_obj_create(panel);
    lv_obj_remove_style_all(chart_row);
    lv_obj_set_width(chart_row, LV_PCT(100));
    lv_obj_set_flex_grow(chart_row, 1);
    lv_obj_set_flex_flow(chart_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chart_row, 6, 0);
    lv_obj_remove_flag(chart_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *y_col = lv_obj_create(chart_row);
    lv_obj_remove_style_all(y_col);
    lv_obj_set_width(y_col, Y_AXIS_WIDTH);
    lv_obj_set_height(y_col, LV_PCT(100));
    lv_obj_set_flex_flow(y_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(y_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(y_col, LV_OBJ_FLAG_SCROLLABLE);
    s_y_max_label = create_axis_label(y_col, LV_TEXT_ALIGN_RIGHT);
    s_y_mid_label = create_axis_label(y_col, LV_TEXT_ALIGN_RIGHT);
    s_y_min_label = create_axis_label(y_col, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_width(s_y_max_label, Y_AXIS_WIDTH);
    lv_obj_set_width(s_y_mid_label, Y_AXIS_WIDTH);
    lv_obj_set_width(s_y_min_label, Y_AXIS_WIDTH);

    s_chart = lv_chart_create(chart_row);
    lv_obj_set_flex_grow(s_chart, 1);
    lv_obj_set_height(s_chart, LV_PCT(100));
    lv_obj_set_style_min_height(s_chart, 180, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_SCATTER);
    lv_chart_set_point_count(s_chart, 1);
    lv_chart_set_div_line_count(s_chart, 4, 6);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(CHART_BG), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(CHART_BORDER), 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(CHART_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 0, LV_PART_ITEMS);
    lv_obj_set_size(s_chart, LV_PCT(100), LV_PCT(100));
    s_series = lv_chart_add_series(s_chart, lv_color_hex(SERIES_COLOR), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_width(s_chart, 14, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_chart, 14, LV_PART_INDICATOR);

    s_empty_label = lv_label_create(panel);
    lv_label_set_text(s_empty_label, "No samples for this metric yet");
    lv_obj_set_style_text_color(s_empty_label, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(s_empty_label, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *x_row = lv_obj_create(panel);
    lv_obj_remove_style_all(x_row);
    lv_obj_set_width(x_row, LV_PCT(100));
    lv_obj_set_height(x_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(x_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(x_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(x_row, Y_AXIS_WIDTH + 6, 0);
    lv_obj_remove_flag(x_row, LV_OBJ_FLAG_SCROLLABLE);
    s_x_left_label = create_axis_label(x_row, LV_TEXT_ALIGN_LEFT);
    s_x_mid_label = create_axis_label(x_row, LV_TEXT_ALIGN_CENTER);
    s_x_right_label = create_axis_label(x_row, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *back = ui_create_back_button(s_screen, back_cb);
    lv_obj_add_flag(back, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_move_foreground(back);
}

void screen_water_sample_chart_show_to(water_sample_metric_t metric, lv_obj_t *return_screen)
{
    if (s_screen == NULL) {
        screen_water_sample_chart_create();
    }

    s_metric = metric;
    s_return_screen = return_screen;

    lv_obj_t *title = lv_obj_get_child(s_screen, 0);
    if (title != NULL) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s - Last 3 Months", water_sample_metric_label(metric));
        lv_label_set_text(title, buf);
    }

    refresh_chart();
    lv_screen_load(s_screen);
}

void screen_water_sample_chart_show(water_sample_metric_t metric)
{
    screen_water_sample_chart_show_to(metric, NULL);
}
