#include "screen_water_sampling.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "maintenance/maintenance_tracker.h"
#include "screen_maintenance_track.h"
#include "screen_water_sample_chart.h"
#include "storage/water_sample_history.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_buttons.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAG_UI "wsample_ui"

#define SKIPPED_VALUE "-"

#define SCREEN_BG_COLOR 0x0D1117
#define PANEL_BG_COLOR  0x161B22
#define BORDER_COLOR    0x30363D
#define TITLE_COLOR     0xE6EDF3
#define LABEL_COLOR     0x8B949E
#define VALUE_COLOR     0xC9D1D9
#define BTN_BG_COLOR    0x21262D
#define BTN_GREEN       0x238636
#define BTN_GREEN_PRESS 0x2EA043
#define BTN_GRAY_PRESS   0x30363D
#define METRIC_INPUT_W  110
#define DATE_INPUT_W    180
#define CHART_BTN_W     190
#define SAVE_BTN_W      200
#define WARN_COLOR      0xD29922

static lv_obj_t *s_screen;
static lv_obj_t *s_date_ta;
static lv_obj_t *s_ph_ta;
static lv_obj_t *s_ammonia_ta;
static lv_obj_t *s_nitrate_ta;
static lv_obj_t *s_nitrite_ta;
static lv_obj_t *s_kh_ta;
static lv_obj_t *s_gh_ta;
static lv_obj_t *s_status_label;
static lv_obj_t *s_storage_warn_label;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_scroll;
static lv_timer_t *s_scroll_timer;
static water_sample_record_t s_pending_record;
static bool s_save_in_progress;
static bool s_save_invalidation_frozen;

static const char *const s_date_kb_map[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", "-", "\n",
    LV_SYMBOL_LEFT, "0", LV_SYMBOL_RIGHT, LV_SYMBOL_CLOSE, "",
};

static const lv_buttonmatrix_ctrl_t s_date_kb_ctrl[] = {
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
};

static const char *const s_metric_kb_map[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", ".", "\n",
    "-", "0", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "",
};

static const lv_buttonmatrix_ctrl_t s_metric_kb_ctrl[] = {
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
};

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
static int64_t s_trace_save_t0_us;
static bool s_trace_save_active;
static uint32_t s_trace_invalidate_count;
static bool s_display_trace_registered;

static int trace_save_ms(void)
{
    if (s_trace_save_t0_us <= 0) {
        return 0;
    }
    return (int)((esp_timer_get_time() - s_trace_save_t0_us) / 1000);
}

static void trace_save_begin(void)
{
    s_trace_save_t0_us = esp_timer_get_time();
    s_trace_save_active = true;
    s_trace_invalidate_count = 0;
    ESP_LOGI(TAG_UI, "TRACE begin t=0");
}

static void trace_save_end_cb(lv_timer_t *timer)
{
    (void)timer;
    ESP_LOGI(TAG_UI, "TRACE end +%dms invalidate_areas=%u", trace_save_ms(),
             (unsigned)s_trace_invalidate_count);
    s_trace_save_active = false;
    s_trace_save_t0_us = 0;
}

static void trace_save_schedule_end(void)
{
    lv_timer_create(trace_save_end_cb, 200, NULL);
}

static const char *focused_ta_name(void)
{
    struct {
        lv_obj_t *ta;
        const char *name;
    } fields[] = {
        {s_date_ta, "date"},
        {s_ph_ta, "ph"},
        {s_ammonia_ta, "ammonia"},
        {s_nitrate_ta, "nitrate"},
        {s_nitrite_ta, "nitrite"},
        {s_kh_ta, "kh"},
        {s_gh_ta, "gh"},
    };

    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (fields[i].ta != NULL && lv_obj_has_state(fields[i].ta, LV_STATE_FOCUSED)) {
            return fields[i].name;
        }
    }
    return "none";
}

static void log_input_state(const char *label)
{
    const bool kb_hidden =
        s_keyboard == NULL || lv_obj_has_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    const lv_coord_t scroll_pad = s_scroll != NULL ? lv_obj_get_style_pad_bottom(s_scroll, 0) : -1;
    const lv_obj_t *kb_ta = s_keyboard != NULL ? lv_keyboard_get_textarea(s_keyboard) : NULL;

    ESP_LOGI(TAG_UI, "%s +%dms focus=%s kb_hidden=%d kb_ta=%p scroll_pad=%d", label, trace_save_ms(),
             focused_ta_name(), (int)kb_hidden, (void *)kb_ta, (int)scroll_pad);
}

static const char *lv_event_code_name(lv_event_code_t code)
{
    switch (code) {
    case LV_EVENT_PRESSED:
        return "PRESSED";
    case LV_EVENT_RELEASED:
        return "RELEASED";
    case LV_EVENT_CLICKED:
        return "CLICKED";
    case LV_EVENT_DEFOCUSED:
        return "DEFOCUSED";
    case LV_EVENT_FOCUSED:
        return "FOCUSED";
    default:
        return "?";
    }
}

static void display_invalidate_area_cb(lv_event_t *e)
{
    if (!s_trace_save_active) {
        return;
    }

    const lv_area_t *area = lv_event_get_invalidated_area(e);
    s_trace_invalidate_count++;
    if (area != NULL) {
        ESP_LOGI(TAG_UI, "INVALIDATE #%u +%dms (%d,%d)-(%d,%d)", (unsigned)s_trace_invalidate_count,
                 trace_save_ms(), (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
    } else {
        ESP_LOGI(TAG_UI, "INVALIDATE #%u +%dms (null area)", (unsigned)s_trace_invalidate_count, trace_save_ms());
    }
}

static void register_display_invalidate_trace(void)
{
    if (s_display_trace_registered) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp == NULL) {
        return;
    }

    lv_display_add_event_cb(disp, display_invalidate_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    s_display_trace_registered = true;
    ESP_LOGI(TAG_UI, "display invalidate-area trace registered");
}

static void save_btn_trace_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        return;
    }
    ESP_LOGI(TAG_UI, "SAVE btn %s +%dms", lv_event_code_name(code), trace_save_ms());
}
#endif /* CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE */

static void freeze_save_display(void);
static void thaw_save_display(bool redraw);

static void save_btn_pressed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }

    freeze_save_display();

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    if (!s_trace_save_active) {
        trace_save_begin();
    }
    ESP_LOGI(TAG_UI, "SAVE btn PRESSED +%dms", trace_save_ms());
#endif
}

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
#define TRACE_SAVE_ABORT()          \
    do {                            \
        thaw_save_display(true);    \
        trace_save_schedule_end();  \
    } while (0)
#else
#define TRACE_SAVE_ABORT() thaw_save_display(true)
#endif

static void refresh_storage_warning(bool retry_attach);
static void reset_metric_fields(void);

static void apply_save_result_ui(bool ok);

static void freeze_save_display(void)
{
    if (s_save_invalidation_frozen) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
        lv_display_enable_invalidation(disp, false);
        s_save_invalidation_frozen = true;
    }
}

static void thaw_save_display(bool redraw)
{
    if (!s_save_invalidation_frozen) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
        lv_display_enable_invalidation(disp, true);
        if (redraw && s_screen != NULL) {
            lv_obj_invalidate(s_screen);
        }
    }
    s_save_invalidation_frozen = false;
}

static void apply_screen_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
}

static void show_status(const char *text)
{
    if (s_status_label != NULL && text != NULL) {
        lv_label_set_text(s_status_label, text);
    }
}

static void refresh_storage_warning(bool retry_attach)
{
    if (s_storage_warn_label == NULL) {
        return;
    }

    const bool sd_ok =
        retry_attach ? water_sample_history_try_attach_storage() : water_sample_history_storage_ready();
    if (sd_ok) {
        lv_obj_add_flag(s_storage_warn_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_storage_warn_label,
                          "Warning: SD card unavailable. Samples are kept in memory only and will be lost on reboot.");
        lv_obj_clear_flag(s_storage_warn_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void reset_metric_fields(void)
{
    lv_obj_t *fields[] = {s_ph_ta, s_ammonia_ta, s_nitrate_ta, s_nitrite_ta, s_kh_ta, s_gh_ta};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (fields[i] != NULL) {
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
            ESP_LOGI(TAG_UI, "SAVE reset ta[%u] +%dms", (unsigned)i, trace_save_ms());
#endif
            lv_textarea_set_text(fields[i], SKIPPED_VALUE);
        }
    }
}

static void apply_save_result_ui(bool ok)
{
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE ui batch start ok=%d +%dms", (int)ok, trace_save_ms());
#endif

    if (ok) {
        reset_metric_fields();
        refresh_storage_warning(false);
        show_status("Sample saved.");
    } else {
        refresh_storage_warning(true);
        show_status("Could not save sample.");
    }

    thaw_save_display(true);

    s_save_in_progress = false;

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE ui batch done +%dms", trace_save_ms());
    trace_save_schedule_end();
#endif
}

static void save_ui_timer_cb(lv_timer_t *timer)
{
    const bool ok = (bool)(intptr_t)lv_timer_get_user_data(timer);
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE ui timer ok=%d +%dms", (int)ok, trace_save_ms());
#endif
    apply_save_result_ui(ok);
    lv_timer_delete(timer);
}

static void schedule_save_ui(bool ok)
{
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE schedule ui ok=%d +%dms", (int)ok, trace_save_ms());
#endif
    if (bsp_display_lock(5000)) {
        lv_timer_create(save_ui_timer_cb, 0, (void *)(intptr_t)ok);
        bsp_display_unlock();
        return;
    }

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGW(TAG_UI, "SAVE display lock failed for ui schedule +%dms", trace_save_ms());
#endif
    s_save_in_progress = false;
    thaw_save_display(true);
}

static void save_worker_task(void *arg)
{
    (void)arg;

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE worker start +%dms", trace_save_ms());
#endif

    (void)water_sample_history_try_attach_storage();
    const bool saved = water_sample_history_add(&s_pending_record);
    if (saved) {
        maintenance_complete(MAINT_WATER_SAMPLING);
    }

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE worker end saved=%d +%dms", (int)saved, trace_save_ms());
#endif

    schedule_save_ui(saved);
    vTaskDelete(NULL);
}

static void hide_keyboard(void)
{
    if (s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_scroll != NULL) {
        lv_obj_set_style_pad_bottom(s_scroll, 0, 0);
    }
    if (s_scroll_timer != NULL) {
        lv_timer_delete(s_scroll_timer);
        s_scroll_timer = NULL;
    }
}

static void scroll_ta_into_view(lv_obj_t *ta)
{
    if (ta == NULL) {
        return;
    }

    if (s_scroll != NULL && s_keyboard != NULL) {
        const lv_coord_t kb_h = lv_obj_get_height(s_keyboard);
        lv_obj_set_style_pad_bottom(s_scroll, kb_h + 16, 0);
    }

    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static void scroll_ta_timer_cb(lv_timer_t *timer)
{
    scroll_ta_into_view((lv_obj_t *)lv_timer_get_user_data(timer));
    lv_timer_delete(timer);
    s_scroll_timer = NULL;
}

static void schedule_scroll_to_ta(lv_obj_t *ta)
{
    if (s_scroll_timer != NULL) {
        lv_timer_delete(s_scroll_timer);
        s_scroll_timer = NULL;
    }

    scroll_ta_into_view(ta);
    s_scroll_timer = lv_timer_create(scroll_ta_timer_cb, 50, ta);
}

static void position_keyboard(void)
{
    if (s_keyboard == NULL) {
        return;
    }
    lv_obj_set_width(s_keyboard, LV_PCT(100));
    lv_obj_set_height(s_keyboard, 220);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "FOCUS ta=%p +%dms", (void *)ta, trace_save_ms());
#endif
    if (s_keyboard == NULL) {
        return;
    }

    if (ta == s_date_ta) {
        lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_USER_1);
    } else {
        lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_USER_2);
    }

    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard);
    position_keyboard();
    schedule_scroll_to_ta(ta);
}

static void ta_defocus_cb(lv_event_t *e)
{
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "DEFOCUS ta=%p +%dms", (void *)lv_event_get_target(e), trace_save_ms());
#endif
    hide_keyboard();
}

static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    hide_keyboard();
    if (ta != NULL) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

static lv_obj_t *create_field_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    return lbl;
}

static void style_sampling_textarea(lv_obj_t *ta)
{
    lv_obj_remove_style_all(ta);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(BORDER_COLOR), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(ta, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_anim_duration(ta, 0, 0);
    lv_obj_set_style_pad_left(ta, 8, 0);
    lv_obj_set_style_pad_right(ta, 8, 0);
    lv_obj_set_style_pad_top(ta, 6, 0);
    lv_obj_set_style_pad_bottom(ta, 6, 0);

    lv_obj_t *label = lv_textarea_get_label(ta);
    if (label != NULL) {
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_SELECTED);
        lv_obj_set_style_text_color(label, lv_color_hex(VALUE_COLOR), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    }
}

static lv_obj_t *create_text_field(lv_obj_t *parent, const char *placeholder, lv_coord_t width)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 16);
    lv_textarea_set_text(ta, placeholder);
    style_sampling_textarea(ta);
    lv_obj_set_width(ta, width);
    lv_obj_set_height(ta, 40);
    lv_obj_set_style_max_width(ta, width, 0);
    lv_obj_set_style_min_width(ta, width, 0);
    lv_obj_set_flex_grow(ta, 0);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    return ta;
}

static void style_sampling_keyboard(lv_obj_t *kb)
{
    lv_obj_set_style_bg_color(kb, lv_color_hex(PANEL_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 4, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(BTN_BG_COLOR), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(BTN_GRAY_PRESS), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(VALUE_COLOR), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_outline_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_anim_duration(kb, 0, LV_PART_ITEMS);
}

static void fill_today_date(void)
{
    if (s_date_ta == NULL) {
        return;
    }

    const time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    lv_textarea_set_text(s_date_ta, buf);
}

static bool parse_date_ymd(const char *text, int *year, int *month, int *day)
{
    if (text == NULL || year == NULL || month == NULL || day == NULL) {
        return false;
    }
    return sscanf(text, "%d-%d-%d", year, month, day) == 3;
}

static bool parse_float_field(const char *text, float *out, float min_v, float max_v)
{
    if (text == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    const float v = strtof(text, &end);
    if (end == text || (end != NULL && *end != '\0')) {
        return false;
    }
    if (v < min_v || v > max_v) {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_optional_float_field(const char *text, float *out, float min_v, float max_v)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    if (strcmp(text, SKIPPED_VALUE) == 0 || text[0] == '\0') {
        *out = NAN;
        return true;
    }
    return parse_float_field(text, out, min_v, max_v);
}

static bool record_has_any_metric(const water_sample_record_t *record)
{
    return !isnan(record->ph) || !isnan(record->ammonia_ppm) || !isnan(record->nitrate_ppm) ||
           !isnan(record->nitrite_ppm) || !isnan(record->kh_deg) || !isnan(record->gh_deg);
}

static time_t epoch_from_local_date(int year, int month, int day)
{
    struct tm local = {0};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    return mktime(&local);
}

static void view_chart_cb(lv_event_t *e)
{
    const water_sample_metric_t metric = (water_sample_metric_t)(intptr_t)lv_event_get_user_data(e);
    hide_keyboard();
    screen_water_sample_chart_show(metric);
}

static lv_obj_t *create_metric_row(lv_obj_t *parent, const char *label, lv_obj_t **ta_out, water_sample_metric_t metric)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = create_field_label(row, label);
    lv_obj_set_width(lbl, 150);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

    *ta_out = create_text_field(row, SKIPPED_VALUE, METRIC_INPUT_W);

    lv_obj_t *btn = lv_button_create(row);
    ui_style_flat_button(btn, BTN_BG_COLOR, BTN_GRAY_PRESS);
    lv_obj_set_size(btn, CHART_BTN_W, 36);
    lv_obj_add_event_cb(btn, view_chart_cb, LV_EVENT_CLICKED, (void *)(intptr_t)metric);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "View Past Results");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    return row;
}

static void save_deferred_cb(lv_timer_t *timer)
{
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE defer start +%dms", trace_save_ms());
    log_input_state("SAVE defer");
#endif

    hide_keyboard();
    lv_timer_delete(timer);

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_SKIP_SD
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE defer skip_sd -> ui only +%dms", trace_save_ms());
#endif
    maintenance_complete(MAINT_WATER_SAMPLING);
    apply_save_result_ui(true);
    return;
#endif

    if (xTaskCreate(save_worker_task, "wsample_save", 8192, NULL, 4, NULL) != pdPASS) {
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
        ESP_LOGW(TAG_UI, "SAVE worker create failed +%dms", trace_save_ms());
#endif
        apply_save_result_ui(false);
    }
}

static void save_cb(lv_event_t *e)
{
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    log_input_state("SAVE tap");
#endif

    ui_button_clear_pressed(lv_event_get_target(e));

#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE after btn clear +%dms", trace_save_ms());
    log_input_state("SAVE after btn clear");
#endif

    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_date_ymd(lv_textarea_get_text(s_date_ta), &year, &month, &day)) {
        show_status("Date must be YYYY-MM-DD.");
        TRACE_SAVE_ABORT();
        return;
    }

    water_sample_record_t record = {0};
    record.epoch = (int32_t)epoch_from_local_date(year, month, day);
    if (record.epoch <= 0) {
        show_status("Invalid date.");
        TRACE_SAVE_ABORT();
        return;
    }

    record.ph = NAN;
    record.ammonia_ppm = NAN;
    record.nitrate_ppm = NAN;
    record.nitrite_ppm = NAN;
    record.kh_deg = NAN;
    record.gh_deg = NAN;

    if (!parse_optional_float_field(lv_textarea_get_text(s_ph_ta), &record.ph, 0.0f, 14.0f)) {
        show_status("pH must be 0.0-14.0 or -.");
        TRACE_SAVE_ABORT();
        return;
    }
    if (!parse_optional_float_field(lv_textarea_get_text(s_ammonia_ta), &record.ammonia_ppm, 0.0f, 999.0f)) {
        show_status("Ammonia must be a valid ppm value or -.");
        TRACE_SAVE_ABORT();
        return;
    }
    if (!parse_optional_float_field(lv_textarea_get_text(s_nitrate_ta), &record.nitrate_ppm, 0.0f, 999.0f)) {
        show_status("Nitrate must be a valid ppm value or -.");
        TRACE_SAVE_ABORT();
        return;
    }
    if (!parse_optional_float_field(lv_textarea_get_text(s_nitrite_ta), &record.nitrite_ppm, 0.0f, 999.0f)) {
        show_status("Nitrite must be a valid ppm value or -.");
        TRACE_SAVE_ABORT();
        return;
    }
    if (!parse_optional_float_field(lv_textarea_get_text(s_kh_ta), &record.kh_deg, 0.0f, 999.0f)) {
        show_status("KH must be a valid value or -.");
        TRACE_SAVE_ABORT();
        return;
    }
    if (!parse_optional_float_field(lv_textarea_get_text(s_gh_ta), &record.gh_deg, 0.0f, 999.0f)) {
        show_status("GH must be a valid value or -.");
        TRACE_SAVE_ABORT();
        return;
    }

    if (!record_has_any_metric(&record)) {
        show_status("Enter at least one test value.");
        TRACE_SAVE_ABORT();
        return;
    }

    if (s_save_in_progress) {
        TRACE_SAVE_ABORT();
        return;
    }

    s_pending_record = record;
    s_save_in_progress = true;
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    ESP_LOGI(TAG_UI, "SAVE validated, defer in 10ms +%dms", trace_save_ms());
#endif
    lv_timer_create(save_deferred_cb, 10, NULL);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    hide_keyboard();
    screen_maintenance_track_show();
}

void screen_water_sampling_create(void)
{
    if (s_screen != NULL) {
        return;
    }

    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) {
        return;
    }
    apply_screen_style(s_screen);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_screen, 8, 0);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Water Sampling");
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    s_storage_warn_label = lv_label_create(s_screen);
    lv_label_set_text(s_storage_warn_label, "");
    lv_label_set_long_mode(s_storage_warn_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_storage_warn_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_storage_warn_label, lv_color_hex(WARN_COLOR), 0);
    lv_obj_set_style_text_font(s_storage_warn_label, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(s_storage_warn_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *scroll = lv_obj_create(s_screen);
    s_scroll = scroll;
    lv_obj_remove_style_all(scroll);
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(PANEL_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scroll, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(scroll, 1, 0);
    lv_obj_set_style_radius(scroll, 12, 0);
    lv_obj_set_style_pad_all(scroll, 12, 0);
    lv_obj_set_style_pad_row(scroll, 4, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *date_row = lv_obj_create(scroll);
    lv_obj_remove_style_all(date_row);
    lv_obj_set_width(date_row, LV_PCT(100));
    lv_obj_set_height(date_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(date_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date_row, 10, 0);
    lv_obj_set_style_pad_bottom(date_row, 10, 0);
    lv_obj_remove_flag(date_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *date_lbl = create_field_label(date_row, "Sample date");
    lv_obj_set_width(date_lbl, 150);
    s_date_ta = create_text_field(date_row, "2026-01-01", DATE_INPUT_W);

    create_metric_row(scroll, "pH (0.0-14.0)", &s_ph_ta, WATER_METRIC_PH);
    create_metric_row(scroll, "Ammonia (ppm)", &s_ammonia_ta, WATER_METRIC_AMMONIA);
    create_metric_row(scroll, "Nitrate (ppm)", &s_nitrate_ta, WATER_METRIC_NITRATE);
    create_metric_row(scroll, "Nitrite (ppm)", &s_nitrite_ta, WATER_METRIC_NITRITE);
    create_metric_row(scroll, "KH (degrees)", &s_kh_ta, WATER_METRIC_KH);
    create_metric_row(scroll, "GH (degrees)", &s_gh_ta, WATER_METRIC_GH);

    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_status_label, LV_PCT(100));

    lv_obj_t *save_btn = lv_button_create(s_screen);
    ui_style_flat_button(save_btn, BTN_GREEN, BTN_GREEN_PRESS);
    lv_obj_remove_flag(save_btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_size(save_btn, SAVE_BTN_W, 48);
    lv_obj_add_event_cb(save_btn, save_btn_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
#if CONFIG_AQUAPILOT_WSAMPLE_SAVE_TRACE
    lv_obj_add_event_cb(save_btn, save_btn_trace_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(save_btn, save_btn_trace_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(save_btn, save_btn_trace_cb, LV_EVENT_DEFOCUSED, NULL);
    register_display_invalidate_trace();
#endif
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save Sample");
    lv_obj_set_style_text_color(save_lbl, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_center(save_lbl);

    lv_obj_t *back = lv_button_create(s_screen);
    ui_style_flat_button(back, BTN_BG_COLOR, BTN_GRAY_PRESS);
    lv_obj_set_size(back, 140, 48);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_center(back_lbl);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1, s_date_kb_map, s_date_kb_ctrl);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_2, s_metric_kb_map, s_metric_kb_ctrl);
    style_sampling_keyboard(s_keyboard);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);
    position_keyboard();

    fill_today_date();
    refresh_storage_warning(true);
}

void screen_water_sampling_show(void)
{
    if (s_screen == NULL) {
        screen_water_sampling_create();
    }
    if (s_screen == NULL) {
        return;
    }

    fill_today_date();
    reset_metric_fields();
    refresh_storage_warning(true);
    show_status("");
    s_save_in_progress = false;
    s_save_invalidation_frozen = false;
    lv_screen_load(s_screen);
}
