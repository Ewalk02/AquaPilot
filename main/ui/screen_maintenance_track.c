#include "screen_maintenance_track.h"

#include "maintenance/maintenance_tracker.h"
#include "screen_settings.h"
#include "screen_water_sampling.h"
#include "storage/aquapilot_settings.h"

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
#define WARN_COLOR       0xD29922
#define MAX_INTERVAL_DAYS 255

static const maintenance_activity_t s_display_order[MAINT_BUILTIN_COUNT] = {
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
static lv_obj_t *s_add_btn;
static lv_obj_t *s_add_overlay;
static lv_obj_t *s_add_name_ta;
static lv_obj_t *s_add_freq_ta;
static lv_obj_t *s_add_status_label;

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
static lv_obj_t *create_activity_row(lv_obj_t *parent, maintenance_activity_t id);
static void delay_cb(lv_event_t *e);
static void sync_custom_rows(void);
static void hide_add_dialog(void);
static void show_add_dialog(void);

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

    if (ta == s_add_name_ta) {
        lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    } else {
        lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_USER_1);
    }

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

    if (ta == s_add_freq_ta) {
        return;
    }

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
        if (ta == s_add_freq_ta) {
            lv_obj_clear_state(ta, LV_STATE_FOCUSED);
            return;
        }
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
    hide_add_dialog();
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

static void set_add_status(const char *text)
{
    if (s_add_status_label != NULL && text != NULL) {
        lv_label_set_text(s_add_status_label, text);
    }
}

static void hide_add_dialog(void)
{
    hide_keyboard();
    if (s_add_overlay != NULL) {
        lv_obj_add_flag(s_add_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static int parse_interval_days_from_ta(lv_obj_t *ta, int fallback)
{
    if (ta == NULL) {
        return fallback;
    }

    const char *text = lv_textarea_get_text(ta);
    if (text == NULL || text[0] == '\0') {
        return fallback;
    }

    char *end = NULL;
    const long parsed = strtol(text, &end, 10);
    if (end == text || (end != NULL && *end != '\0')) {
        return fallback;
    }

    int days = (int)parsed;
    if (days < 1) {
        days = 1;
    } else if (days > MAX_INTERVAL_DAYS) {
        days = MAX_INTERVAL_DAYS;
    }
    return days;
}

static void add_task_submit(void)
{
    if (s_add_name_ta == NULL || s_add_freq_ta == NULL) {
        return;
    }

    const char *name = lv_textarea_get_text(s_add_name_ta);
    const int days = parse_interval_days_from_ta(s_add_freq_ta, 7);
    const int id = maintenance_add_custom(name, days);
    if (id < 0) {
        if (maintenance_custom_slots_available() <= 0) {
            set_add_status("Task list is full (max 8 custom tasks).");
        } else {
            set_add_status("Enter a task name and interval.");
        }
        return;
    }

    hide_add_dialog();
    sync_custom_rows();
    refresh_rows();
}

static void add_cancel_cb(lv_event_t *e)
{
    (void)e;
    hide_add_dialog();
}

static void add_submit_cb(lv_event_t *e)
{
    ui_button_clear_pressed(lv_event_get_target(e));
    add_task_submit();
}

static void add_btn_cb(lv_event_t *e)
{
    (void)e;
    show_add_dialog();
}

static void add_panel_click_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
}

static void create_add_dialog(lv_obj_t *parent)
{
    s_add_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_add_overlay);
    lv_obj_set_size(s_add_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_add_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_add_overlay, LV_OPA_60, 0);
    lv_obj_add_flag(s_add_overlay, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_add_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_add_overlay, add_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_add_overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_width(panel, 520);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PANEL_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_event_cb(panel, add_panel_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *heading = lv_label_create(panel);
    lv_label_set_text(heading, "Add Maintenance Task");
    lv_obj_set_style_text_color(heading, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_20, 0);

    lv_obj_t *name_lbl = lv_label_create(panel);
    lv_label_set_text(name_lbl, "Task name");
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);

    s_add_name_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_add_name_ta, true);
    lv_textarea_set_max_length(s_add_name_ta, AQUAPILOT_MAINT_CUSTOM_NAME_LEN - 1);
    lv_textarea_set_placeholder_text(s_add_name_ta, "e.g. Trim plants");
    style_freq_textarea(s_add_name_ta);
    lv_obj_set_width(s_add_name_ta, LV_PCT(100));
    lv_obj_set_height(s_add_name_ta, 40);
    lv_obj_add_event_cb(s_add_name_ta, freq_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_add_name_ta, freq_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *freq_row = lv_obj_create(panel);
    lv_obj_remove_style_all(freq_row);
    lv_obj_set_width(freq_row, LV_PCT(100));
    lv_obj_set_height(freq_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(freq_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(freq_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(freq_row, 8, 0);
    lv_obj_remove_flag(freq_row, LV_OBJ_FLAG_SCROLLABLE);

    create_table_label(freq_row, "Every", LV_SIZE_CONTENT, LV_TEXT_ALIGN_LEFT, LABEL_COLOR, &lv_font_montserrat_16);
    s_add_freq_ta = lv_textarea_create(freq_row);
    lv_textarea_set_one_line(s_add_freq_ta, true);
    lv_textarea_set_max_length(s_add_freq_ta, 3);
    style_freq_textarea(s_add_freq_ta);
    lv_obj_set_width(s_add_freq_ta, FREQ_INPUT_W);
    lv_obj_set_height(s_add_freq_ta, 32);
    lv_textarea_set_text(s_add_freq_ta, "7");
    lv_obj_add_event_cb(s_add_freq_ta, freq_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_add_freq_ta, freq_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    create_table_label(freq_row, "days", LV_SIZE_CONTENT, LV_TEXT_ALIGN_LEFT, LABEL_COLOR, &lv_font_montserrat_16);

    s_add_status_label = lv_label_create(panel);
    lv_label_set_text(s_add_status_label, "");
    lv_obj_set_style_text_color(s_add_status_label, lv_color_hex(WARN_COLOR), 0);
    lv_obj_set_style_text_font(s_add_status_label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_add_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_add_status_label, LV_PCT(100));

    lv_obj_t *btn_row = lv_obj_create(panel);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 12, 0);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel_btn = lv_button_create(btn_row);
    lv_obj_set_size(cancel_btn, 120, 40);
    ui_style_flat_button(cancel_btn, BTN_BG_COLOR, BTN_GRAY_PRESS);
    lv_obj_add_event_cb(cancel_btn, add_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_center(cancel_lbl);

    lv_obj_t *submit_btn = lv_button_create(btn_row);
    lv_obj_set_size(submit_btn, 120, 40);
    ui_style_flat_button(submit_btn, BTN_GREEN, BTN_GREEN_PRESS);
    lv_obj_add_event_cb(submit_btn, add_submit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *submit_lbl = lv_label_create(submit_btn);
    lv_label_set_text(submit_lbl, "Add");
    lv_obj_set_style_text_color(submit_lbl, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_center(submit_lbl);
}

static void show_add_dialog(void)
{
    if (s_add_overlay == NULL) {
        return;
    }

    hide_keyboard();
    if (maintenance_custom_slots_available() <= 0) {
        return;
    }

    if (s_add_name_ta != NULL) {
        lv_textarea_set_text(s_add_name_ta, "");
    }
    if (s_add_freq_ta != NULL) {
        lv_textarea_set_text(s_add_freq_ta, "7");
    }
    set_add_status("");

    lv_obj_clear_flag(s_add_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_add_overlay);
    if (s_keyboard != NULL) {
        lv_obj_move_foreground(s_keyboard);
    }
}

static void sync_custom_rows(void)
{
    if (s_scroll == NULL) {
        return;
    }

    for (int slot = 0; slot < MAINT_CUSTOM_MAX; slot++) {
        const maintenance_activity_t id = MAINT_BUILTIN_COUNT + slot;
        const bool active = maintenance_custom_is_active(id);

        if (active && s_rows[id] == NULL) {
            create_activity_row(s_scroll, id);
        } else if (!active && s_rows[id] != NULL) {
            lv_obj_delete(s_rows[id]);
            s_rows[id] = NULL;
            s_status_labels[id] = NULL;
            s_freq_ta[id] = NULL;
        }
    }
}

static void update_add_btn_state(void)
{
    if (s_add_btn == NULL) {
        return;
    }

    if (maintenance_custom_slots_available() <= 0) {
        lv_obj_add_state(s_add_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_add_btn, LV_STATE_DISABLED);
    }
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
    for (int i = 0; i < MAINT_BUILTIN_COUNT; i++) {
        create_activity_row(scroll, s_display_order[i]);
    }
    sync_custom_rows();

    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);

    ui_create_back_button(s_screen, back_cb);

    s_add_btn = lv_button_create(s_screen);
    lv_obj_set_size(s_add_btn, 168, 48);
    ui_style_flat_button(s_add_btn, BTN_GREEN, BTN_GREEN_PRESS);
    lv_obj_add_flag(s_add_btn, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_add_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(s_add_btn, add_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *add_lbl = lv_label_create(s_add_btn);
    lv_label_set_text(add_lbl, "+ Add Task");
    lv_obj_set_style_text_color(add_lbl, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(add_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(add_lbl);

    create_add_dialog(s_screen);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1, s_days_kb_map, s_days_kb_ctrl);
    style_maint_keyboard(s_keyboard);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_CANCEL, NULL);
    position_keyboard();

    update_add_btn_state();
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

    sync_custom_rows();
    update_add_btn_state();
    refresh_rows();
    if (s_ui_timer == NULL) {
        s_ui_timer = lv_timer_create(ui_timer_cb, 60000, NULL);
    }
    lv_screen_load(s_screen);
}
