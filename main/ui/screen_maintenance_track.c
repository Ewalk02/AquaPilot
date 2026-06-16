#include "screen_maintenance_track.h"

#include "maintenance/maintenance_tracker.h"
#include "screen_settings.h"
#include "screen_water_sampling.h"

#include "lvgl.h"
#include "ui_buttons.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#define SCREEN_BG_COLOR  0x0D1117
#define PANEL_BG_COLOR   0x161B22
#define BORDER_COLOR     0x30363D
#define TITLE_COLOR      0xE6EDF3
#define LABEL_COLOR      0x8B949E
#define VALUE_COLOR      0xC9D1D9
#define BTN_BG_COLOR     0x21262D
#define BTN_GREEN        0x238636
#define BTN_GREEN_PRESS  0x2EA043
#define BTN_GRAY_PRESS   0x30363D

static lv_obj_t *s_screen;
static lv_obj_t *s_status_label;
static lv_obj_t *s_rows[MAINT_ACTIVITY_COUNT];
static lv_obj_t *s_status_labels[MAINT_ACTIVITY_COUNT];
static lv_timer_t *s_ui_timer;

static void refresh_rows(void);

static void apply_screen_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 40);
    lv_obj_set_style_min_width(btn, 120, 0);
    ui_style_flat_button(btn, BTN_BG_COLOR, BTN_GRAY_PRESS);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *create_green_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 40);
    lv_obj_set_style_min_width(btn, 120, 0);
    ui_style_flat_button(btn, BTN_GREEN, BTN_GREEN_PRESS);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static void refresh_rows(void)
{
    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        if (s_rows[i] == NULL) {
            continue;
        }

        lv_obj_t *status = s_status_labels[i];
        if (status != NULL) {
            char due[32];
            const int days = maintenance_days_until_due((maintenance_activity_t)i);
            if (days == INT32_MIN) {
                snprintf(due, sizeof(due), "Time not set");
            } else if (days < 0) {
                snprintf(due, sizeof(due), "Overdue %d d", -days);
            } else if (days == 0) {
                snprintf(due, sizeof(due), "Due today");
            } else {
                snprintf(due, sizeof(due), "Due in %d d", days);
            }
            lv_label_set_text(status, due);
        }
    }
}

static void complete_deferred_cb(lv_timer_t *timer)
{
    const maintenance_activity_t id = (maintenance_activity_t)(intptr_t)lv_timer_get_user_data(timer);
    maintenance_complete(id);
    refresh_rows();
    lv_timer_delete(timer);
}

static void delay_deferred_cb(lv_timer_t *timer)
{
    const maintenance_activity_t id = (maintenance_activity_t)(intptr_t)lv_timer_get_user_data(timer);
    maintenance_delay_one_week(id);
    refresh_rows();
    lv_timer_delete(timer);
}

static void complete_cb(lv_event_t *e)
{
    ui_button_clear_pressed(lv_event_get_target(e));

    const maintenance_activity_t id = (maintenance_activity_t)(intptr_t)lv_event_get_user_data(e);
    lv_timer_create(complete_deferred_cb, 10, (void *)(intptr_t)id);
}

static void delay_cb(lv_event_t *e)
{
    ui_button_clear_pressed(lv_event_get_target(e));

    const maintenance_activity_t id = (maintenance_activity_t)(intptr_t)lv_event_get_user_data(e);
    lv_timer_create(delay_deferred_cb, 10, (void *)(intptr_t)id);
}

static void open_sampling_timer_cb(lv_timer_t *timer)
{
    screen_water_sampling_show();
    lv_timer_delete(timer);
}

static void log_sample_pressed_cb(lv_event_t *e)
{
    ui_button_clear_pressed(lv_event_get_target(e));
    lv_timer_create(open_sampling_timer_cb, 10, NULL);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_ui_timer != NULL) {
        lv_timer_delete(s_ui_timer);
        s_ui_timer = NULL;
    }
    screen_settings_show();
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_rows();
}

static lv_obj_t *create_activity_row(lv_obj_t *parent, maintenance_activity_t id)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 12, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(row);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, maintenance_activity_label(id));
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    lv_obj_t *status = lv_label_create(header);
    lv_label_set_text(status, "--");
    lv_obj_set_style_text_color(status, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    s_status_labels[id] = status;

    lv_obj_t *btn_row = lv_obj_create(row);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    if (id == MAINT_WATER_SAMPLING) {
        create_green_button(btn_row, "Log Sample", log_sample_pressed_cb, NULL);
    } else {
        create_green_button(btn_row, "Complete", complete_cb, (void *)(intptr_t)id);
        create_action_button(btn_row, "Delay 1 Week", delay_cb, (void *)(intptr_t)id);
    }

    return row;
}

void screen_maintenance_track_create(void)
{
    s_screen = lv_obj_create(NULL);
    apply_screen_style(s_screen);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_screen, 12, 0);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Tank Maintenance");
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    lv_obj_t *scroll = lv_obj_create(s_screen);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 4, 0);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(PANEL_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scroll, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(scroll, 1, 0);
    lv_obj_set_style_radius(scroll, 12, 0);
    lv_obj_set_style_pad_all(scroll, 12, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        s_rows[i] = create_activity_row(scroll, (maintenance_activity_t)i);
    }

    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back = lv_button_create(s_screen);
    lv_obj_set_size(back, 140, 48);
    ui_style_flat_button(back, BTN_BG_COLOR, BTN_GRAY_PRESS);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_center(back_lbl);

    refresh_rows();
}

void screen_maintenance_track_show(void)
{
    if (s_screen == NULL) {
        screen_maintenance_track_create();
    }
    if (s_screen == NULL) {
        return;
    }

    refresh_rows();
    if (s_ui_timer == NULL) {
        s_ui_timer = lv_timer_create(ui_timer_cb, 60000, NULL);
    }
    lv_screen_load(s_screen);
}
