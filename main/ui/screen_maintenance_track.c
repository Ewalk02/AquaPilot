#include "screen_maintenance_track.h"

#include "maintenance/maintenance_tracker.h"
#include "screen_settings.h"
#include "screen_water_sampling.h"

#include "lvgl.h"
#include "ui_buttons.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define FREQ_INPUT_W     56
#define COL_TASK_W       200
#define COL_FREQ_W       284
#define COL_DUE_W        80
#define COL_ACTION_W     168
#define COL_DELAY_W      168
#define HEADER_COLOR     0x6E7681
#define MAX_INTERVAL_DAYS 255

static const maintenance_activity_t s_display_order[MAINT_ACTIVITY_COUNT] = {
    MAINT_WATER_SAMPLING,
    MAINT_WATER_CHANGE,
    MAINT_FILTER_CLEANING,
    MAINT_CHECK_CO2,
    MAINT_FILL_FEEDER,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_status_label;
static lv_obj_t *s_rows[MAINT_ACTIVITY_COUNT];
static lv_obj_t *s_status_labels[MAINT_ACTIVITY_COUNT];
static lv_obj_t *s_freq_ta[MAINT_ACTIVITY_COUNT];
static lv_obj_t *s_scroll;
static lv_obj_t *s_keyboard;
static lv_timer_t *s_ui_timer;

static const char *const s_days_kb_map[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", LV_SYMBOL_CLOSE, "\n",
    LV_SYMBOL_LEFT, "0", LV_SYMBOL_RIGHT, LV_SYMBOL_CLOSE, "",
};

static const lv_buttonmatrix_ctrl_t s_days_kb_ctrl[] = {
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
};

static void refresh_rows(void);
static lv_obj_t *create_freq_textarea(lv_obj_t *parent, maintenance_activity_t id);
static void delay_cb(lv_event_t *e);

static void apply_screen_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
}

static void style_freq_textarea(lv_obj_t *ta)
{
    lv_obj_remove_style_all(ta);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(BORDER_COLOR), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(ta, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_anim_duration(ta, 0, 0);
    lv_obj_set_style_pad_left(ta, 6, 0);
    lv_obj_set_style_pad_right(ta, 6, 0);
    lv_obj_set_style_pad_top(ta, 4, 0);
    lv_obj_set_style_pad_bottom(ta, 4, 0);

    lv_obj_t *label = lv_textarea_get_label(ta);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, lv_color_hex(VALUE_COLOR), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    }
}

static void style_maint_keyboard(lv_obj_t *kb)
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

static void hide_keyboard(void)
{
    if (s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_scroll != NULL) {
        lv_obj_set_style_pad_bottom(s_scroll, 0, 0);
    }
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

static void scroll_ta_into_view(lv_obj_t *ta)
{
    if (ta == NULL || s_scroll == NULL) {
        return;
    }

    if (s_keyboard != NULL) {
        const lv_coord_t kb_h = lv_obj_get_height(s_keyboard);
        lv_obj_set_style_pad_bottom(s_scroll, kb_h + 16, 0);
    }

    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static void freq_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (s_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard);
    position_keyboard();
    scroll_ta_into_view(ta);
}

static void apply_freq_from_ta(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || s_freq_ta[id] == NULL) {
        return;
    }

    const char *text = lv_textarea_get_text(s_freq_ta[id]);
    if (text == NULL || text[0] == '\0') {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", maintenance_interval_days(id));
        lv_textarea_set_text(s_freq_ta[id], buf);
        return;
    }

    char *end = NULL;
    const long parsed = strtol(text, &end, 10);
    if (end == text || (end != NULL && *end != '\0')) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", maintenance_interval_days(id));
        lv_textarea_set_text(s_freq_ta[id], buf);
        return;
    }

    int days = (int)parsed;
    if (days < 1) {
        days = 1;
    } else if (days > MAX_INTERVAL_DAYS) {
        days = MAX_INTERVAL_DAYS;
    }

    maintenance_set_interval_days(id, days);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", days);
    lv_textarea_set_text(s_freq_ta[id], buf);
}

static void freq_defocus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    hide_keyboard();

    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        if (s_freq_ta[i] == ta) {
            apply_freq_from_ta((maintenance_activity_t)i);
            break;
        }
    }
}

static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    hide_keyboard();
    if (ta != NULL) {
        for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
            if (s_freq_ta[i] == ta) {
                apply_freq_from_ta((maintenance_activity_t)i);
                lv_obj_clear_state(ta, LV_STATE_FOCUSED);
                break;
            }
        }
    }
}

static lv_obj_t *create_table_label(lv_obj_t *parent, const char *text, lv_coord_t width, lv_text_align_t align,
                                    uint32_t color, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, width);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_align(lbl, align, 0);
    if (align == LV_TEXT_ALIGN_LEFT) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }
    return lbl;
}

static lv_obj_t *create_table_row(lv_obj_t *parent, bool header)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, header ? 22 : 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    if (!header) {
        lv_obj_set_style_pad_bottom(row, 6, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(BORDER_COLOR), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_50, 0);
    } else {
        lv_obj_set_style_border_color(row, lv_color_hex(BORDER_COLOR), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_bottom(row, 6, 0);
    }
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *create_freq_cell(lv_obj_t *parent, maintenance_activity_t id)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_width(cell, COL_FREQ_W);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cell, 8, 0);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    create_table_label(cell, "Every", LV_SIZE_CONTENT, LV_TEXT_ALIGN_LEFT, LABEL_COLOR, &lv_font_montserrat_16);
    create_freq_textarea(cell, id);
    create_table_label(cell, "days", LV_SIZE_CONTENT, LV_TEXT_ALIGN_LEFT, LABEL_COLOR, &lv_font_montserrat_16);
    return cell;
}

static lv_obj_t *create_column_cell(lv_obj_t *parent, lv_coord_t width)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_width(cell, width);
    lv_obj_set_height(cell, 36);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    return cell;
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data,
                                      bool green)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, COL_ACTION_W, 36);
    if (green) {
        ui_style_flat_button(btn, BTN_GREEN, BTN_GREEN_PRESS);
    } else {
        ui_style_flat_button(btn, BTN_BG_COLOR, BTN_GRAY_PRESS);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(green ? TITLE_COLOR : VALUE_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *create_delay_button(lv_obj_t *parent, maintenance_activity_t id)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, COL_DELAY_W, 36);
    ui_style_flat_button(btn, BTN_BG_COLOR, BTN_GRAY_PRESS);
    lv_obj_add_event_cb(btn, delay_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Delay 1 Week");
    lv_obj_set_style_text_color(lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static void create_header_row(lv_obj_t *parent)
{
    lv_obj_t *row = create_table_row(parent, true);

    create_table_label(row, "Task", COL_TASK_W, LV_TEXT_ALIGN_LEFT, HEADER_COLOR, &lv_font_montserrat_12);
    create_table_label(row, "Every (days)", COL_FREQ_W, LV_TEXT_ALIGN_LEFT, HEADER_COLOR, &lv_font_montserrat_12);
    create_table_label(row, "Due", COL_DUE_W, LV_TEXT_ALIGN_CENTER, HEADER_COLOR, &lv_font_montserrat_12);
    create_table_label(row, "Action", COL_ACTION_W, LV_TEXT_ALIGN_CENTER, HEADER_COLOR, &lv_font_montserrat_12);
    create_table_label(row, "Delay", COL_DELAY_W, LV_TEXT_ALIGN_CENTER, HEADER_COLOR, &lv_font_montserrat_12);
}

static void refresh_freq_field(maintenance_activity_t id)
{
    if (id < 0 || id >= MAINT_ACTIVITY_COUNT || s_freq_ta[id] == NULL) {
        return;
    }

    if (lv_obj_has_state(s_freq_ta[id], LV_STATE_FOCUSED)) {
        return;
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", maintenance_interval_days(id));
    lv_textarea_set_text(s_freq_ta[id], buf);
}

static void refresh_rows(void)
{
    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        refresh_freq_field((maintenance_activity_t)i);

        if (s_rows[i] == NULL) {
            continue;
        }

        lv_obj_t *status = s_status_labels[i];
        if (status != NULL) {
            char due[32];
            const int days = maintenance_days_until_due((maintenance_activity_t)i);
            if (days == INT32_MIN) {
                snprintf(due, sizeof(due), "--");
            } else if (days < 0) {
                snprintf(due, sizeof(due), "+%dd", -days);
            } else if (days == 0) {
                snprintf(due, sizeof(due), "Today");
            } else {
                snprintf(due, sizeof(due), "%dd", days);
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
    hide_keyboard();
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
    hide_keyboard();
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

static lv_obj_t *create_freq_textarea(lv_obj_t *parent, maintenance_activity_t id)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 3);
    style_freq_textarea(ta);
    lv_obj_set_width(ta, FREQ_INPUT_W);
    lv_obj_set_height(ta, 32);
    lv_obj_add_event_cb(ta, freq_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, freq_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    s_freq_ta[id] = ta;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", maintenance_interval_days(id));
    lv_textarea_set_text(ta, buf);
    return ta;
}

static lv_obj_t *create_activity_row(lv_obj_t *parent, maintenance_activity_t id)
{
    lv_obj_t *row = create_table_row(parent, false);

    create_table_label(row, maintenance_activity_label(id), COL_TASK_W, LV_TEXT_ALIGN_LEFT, TITLE_COLOR,
                       &lv_font_montserrat_16);

    create_freq_cell(row, id);

    lv_obj_t *status = create_table_label(row, "--", COL_DUE_W, LV_TEXT_ALIGN_CENTER, LABEL_COLOR,
                                          &lv_font_montserrat_14);
    s_status_labels[id] = status;

    lv_obj_t *action_cell = create_column_cell(row, COL_ACTION_W);
    if (id == MAINT_WATER_SAMPLING) {
        create_action_button(action_cell, "Log Sample", log_sample_pressed_cb, NULL, true);
    } else {
        create_action_button(action_cell, "Complete", complete_cb, (void *)(intptr_t)id, true);
    }

    lv_obj_t *delay_cell = create_column_cell(row, COL_DELAY_W);
    if (id != MAINT_WATER_SAMPLING) {
        create_delay_button(delay_cell, id);
    }

    s_rows[id] = row;
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
    s_scroll = scroll;
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

    create_header_row(scroll);
    for (int i = 0; i < MAINT_ACTIVITY_COUNT; i++) {
        create_activity_row(scroll, s_display_order[i]);
    }

    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);

    ui_create_back_button(s_screen, back_cb);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1, s_days_kb_map, s_days_kb_ctrl);
    style_maint_keyboard(s_keyboard);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);
    position_keyboard();

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
