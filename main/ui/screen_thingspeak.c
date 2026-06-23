#include "screen_thingspeak.h"

#include "net/thingspeak_uploader.h"
#include "screen_settings.h"
#include "storage/aquapilot_settings.h"
#include "ui_buttons.h"

#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_BG_COLOR 0x0D1117
#define PANEL_BG_COLOR  0x161B22
#define BORDER_COLOR    0x30363D
#define TITLE_COLOR     0xE6EDF3
#define LABEL_COLOR     0x8B949E
#define VALUE_COLOR     0xC9D1D9
#define STATUS_COLOR    0x6E7681
#define BTN_BG_COLOR    0x21262D
#define HEADER_COLOR    0x6E7681
#define COL_FIELD_W     96
#define COL_SOURCE_W    420
#define COL_DURATION_W  160
#define TABLE_ROW_H     44

static const char *const s_metric_options =
    "Off\n"
    "Temperature (F)\n"
    "Filter Power (W)\n"
    "CO2 Power (W)\n"
    "Feed Status\n"
    "Air Pump Power (W)";

static const char *const s_interval_options =
    "Off\n"
    "5 min\n"
    "15 min\n"
    "30 min\n"
    "60 min\n"
    "2 hr";

static const uint16_t s_interval_values[] = {0, 5, 15, 30, 60, 120};

static lv_obj_t *s_screen;
static lv_obj_t *s_enabled_sw;
static lv_obj_t *s_api_key_ta;
static lv_obj_t *s_channel_ta;
static lv_obj_t *s_status_label;
static lv_obj_t *s_metric_dd[AQUAPILOT_THINGSPEAK_FIELD_COUNT];
static lv_obj_t *s_interval_dd[AQUAPILOT_THINGSPEAK_FIELD_COUNT];
static lv_obj_t *s_keyboard;
static lv_timer_t *s_status_timer;

static void apply_screen_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
}

static void style_settings_input(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(BTN_BG_COLOR), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, 0);
    lv_obj_set_style_radius(obj, 8, 0);
}

static lv_obj_t *create_field_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    return lbl;
}

static lv_obj_t *create_form_panel(lv_obj_t *parent)
{
    lv_obj_t *form = lv_obj_create(parent);
    lv_obj_remove_style_all(form);
    lv_obj_set_width(form, LV_PCT(100));
    lv_obj_set_height(form, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(form, lv_color_hex(PANEL_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(form, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(form, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(form, 2, 0);
    lv_obj_set_style_radius(form, 12, 0);
    lv_obj_set_style_pad_all(form, 20, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(form, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(form, 8, 0);
    return form;
}

static void hide_keyboard(void)
{
    if (s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void position_keyboard(void)
{
    if (s_keyboard == NULL) {
        return;
    }
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void show_keyboard_for_textarea(lv_obj_t *ta, lv_keyboard_mode_t mode)
{
    if (s_keyboard == NULL || ta == NULL) {
        return;
    }
    lv_keyboard_set_mode(s_keyboard, mode);
    lv_keyboard_set_textarea(s_keyboard, ta);
    position_keyboard();
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
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

static void keyboard_cancel_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    hide_keyboard();
    if (ta != NULL) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

static void api_key_focus_cb(lv_event_t *e)
{
    show_keyboard_for_textarea(lv_event_get_target(e), LV_KEYBOARD_MODE_TEXT_LOWER);
}

static void channel_focus_cb(lv_event_t *e)
{
    show_keyboard_for_textarea(lv_event_get_target(e), LV_KEYBOARD_MODE_NUMBER);
}

static bool save_credentials(void);

static void credential_defocus_cb(lv_event_t *e)
{
    (void)e;
    hide_keyboard();
    (void)save_credentials();
}

static uint32_t metric_to_dropdown_index(aquapilot_ts_metric_t metric)
{
    switch (metric) {
    case AQUAPILOT_TS_METRIC_TEMP_F:
        return 1;
    case AQUAPILOT_TS_METRIC_FILTER_W:
        return 2;
    case AQUAPILOT_TS_METRIC_CO2_W:
        return 3;
    case AQUAPILOT_TS_METRIC_FEED_STATUS:
        return 4;
    case AQUAPILOT_TS_METRIC_AIR_W:
        return 5;
    default:
        return 0;
    }
}

static aquapilot_ts_metric_t dropdown_index_to_metric(uint32_t index)
{
    switch (index) {
    case 1:
        return AQUAPILOT_TS_METRIC_TEMP_F;
    case 2:
        return AQUAPILOT_TS_METRIC_FILTER_W;
    case 3:
        return AQUAPILOT_TS_METRIC_CO2_W;
    case 4:
        return AQUAPILOT_TS_METRIC_FEED_STATUS;
    case 5:
        return AQUAPILOT_TS_METRIC_AIR_W;
    default:
        return AQUAPILOT_TS_METRIC_NONE;
    }
}

static uint32_t interval_to_dropdown_index(uint16_t interval_min)
{
    for (uint32_t i = 0; i < sizeof(s_interval_values) / sizeof(s_interval_values[0]); i++) {
        if (s_interval_values[i] == interval_min) {
            return i;
        }
    }
    return 0;
}

static uint16_t dropdown_index_to_interval(uint32_t index)
{
    if (index >= sizeof(s_interval_values) / sizeof(s_interval_values[0])) {
        return 0;
    }
    return s_interval_values[index];
}

static bool save_field(uint8_t field_idx)
{
    if (field_idx >= AQUAPILOT_THINGSPEAK_FIELD_COUNT) {
        return false;
    }

    const uint32_t metric_idx = lv_dropdown_get_selected(s_metric_dd[field_idx]);
    const uint32_t interval_idx = lv_dropdown_get_selected(s_interval_dd[field_idx]);
    return aquapilot_settings_set_thingspeak_field(field_idx, dropdown_index_to_metric(metric_idx),
                                                   dropdown_index_to_interval((uint16_t)interval_idx));
}

static bool save_credentials(void)
{
    const char *api_key = lv_textarea_get_text(s_api_key_ta);
    const char *channel = lv_textarea_get_text(s_channel_ta);

    if (api_key != NULL && api_key[0] != '\0') {
        if (!aquapilot_settings_set_thingspeak_api_key(api_key)) {
            return false;
        }
    } else if (!aquapilot_settings_set_thingspeak_api_key("")) {
        return false;
    }

    if (channel == NULL || channel[0] == '\0') {
        return false;
    }
    return aquapilot_settings_set_thingspeak_channel_id(channel);
}

static void thingspeak_save_from_fields(void)
{
    bool enabled = lv_obj_has_state(s_enabled_sw, LV_STATE_CHECKED);
    aquapilot_settings_set_thingspeak_enabled(enabled);
    (void)save_credentials();
    for (uint8_t i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        (void)save_field(i);
    }
}

static void enabled_switch_cb(lv_event_t *e)
{
    (void)e;
    thingspeak_save_from_fields();
}

static void field_changed_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    for (uint8_t i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        if (target == s_metric_dd[i] || target == s_interval_dd[i]) {
            (void)save_field(i);
            return;
        }
    }
}

static void thingspeak_refresh_fields(void)
{
    bool enabled = false;
    aquapilot_settings_get_thingspeak_enabled(&enabled);
    if (enabled) {
        lv_obj_add_state(s_enabled_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_enabled_sw, LV_STATE_CHECKED);
    }

    char buf[AQUAPILOT_THINGSPEAK_KEY_MAX + 1];
    aquapilot_settings_get_thingspeak_api_key(buf, sizeof(buf));
    lv_textarea_set_text(s_api_key_ta, buf);

    aquapilot_settings_get_thingspeak_channel_id(buf, sizeof(buf));
    lv_textarea_set_text(s_channel_ta, buf);

    for (uint8_t i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        aquapilot_ts_metric_t metric = AQUAPILOT_TS_METRIC_NONE;
        uint16_t interval_min = 0;
        aquapilot_settings_get_thingspeak_field(i, &metric, &interval_min);
        lv_dropdown_set_selected(s_metric_dd[i], metric_to_dropdown_index(metric));
        lv_dropdown_set_selected(s_interval_dd[i], interval_to_dropdown_index(interval_min));
    }

    if (s_status_label != NULL) {
        lv_label_set_text(s_status_label, thingspeak_uploader_last_status_text());
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_status_label != NULL) {
        lv_label_set_text(s_status_label, thingspeak_uploader_last_status_text());
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    hide_keyboard();
    thingspeak_save_from_fields();
    screen_settings_show();
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
    lv_obj_set_height(row, header ? 24 : TABLE_ROW_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(row, header ? 1 : 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_opa(row, header ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_pad_bottom(row, header ? 6 : 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *create_column_cell(lv_obj_t *parent, lv_coord_t width)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_width(cell, width);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    return cell;
}

static lv_obj_t *create_dropdown(lv_obj_t *parent, lv_coord_t width)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    style_settings_input(dd);
    lv_obj_set_width(dd, width);
    lv_obj_set_style_bg_color(dd, lv_color_hex(BTN_BG_COLOR), 0);
    return dd;
}

static void create_fields_header_row(lv_obj_t *parent)
{
    lv_obj_t *row = create_table_row(parent, true);
    create_table_label(row, "Field", COL_FIELD_W, LV_TEXT_ALIGN_LEFT, HEADER_COLOR, &lv_font_montserrat_12);
    create_table_label(row, "Data Source", COL_SOURCE_W, LV_TEXT_ALIGN_LEFT, HEADER_COLOR, &lv_font_montserrat_12);
    create_table_label(row, "Duration", COL_DURATION_W, LV_TEXT_ALIGN_LEFT, HEADER_COLOR, &lv_font_montserrat_12);
}

static lv_obj_t *create_field_row(lv_obj_t *parent, uint8_t field_idx)
{
    lv_obj_t *row = create_table_row(parent, false);

    char label[16];
    snprintf(label, sizeof(label), "Field %u", (unsigned)(field_idx + 1));
    create_table_label(row, label, COL_FIELD_W, LV_TEXT_ALIGN_LEFT, VALUE_COLOR, &lv_font_montserrat_16);

    lv_obj_t *source_cell = create_column_cell(row, COL_SOURCE_W);
    s_metric_dd[field_idx] = create_dropdown(source_cell, COL_SOURCE_W - 8);
    lv_dropdown_set_options_static(s_metric_dd[field_idx], s_metric_options);
    lv_obj_add_event_cb(s_metric_dd[field_idx], field_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *duration_cell = create_column_cell(row, COL_DURATION_W);
    s_interval_dd[field_idx] = create_dropdown(duration_cell, COL_DURATION_W - 8);
    lv_dropdown_set_options_static(s_interval_dd[field_idx], s_interval_options);
    lv_obj_add_event_cb(s_interval_dd[field_idx], field_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return row;
}

static lv_obj_t *create_fields_table(lv_obj_t *parent)
{
    lv_obj_t *table = lv_obj_create(parent);
    lv_obj_remove_style_all(table);
    lv_obj_set_width(table, LV_PCT(100));
    lv_obj_set_height(table, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(table, 0, 0);
    lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);

    create_fields_header_row(table);
    for (uint8_t i = 0; i < AQUAPILOT_THINGSPEAK_FIELD_COUNT; i++) {
        create_field_row(table, i);
    }
    return table;
}

void screen_thingspeak_create(void)
{
    if (s_screen != NULL) {
        return;
    }

    s_screen = lv_obj_create(NULL);
    apply_screen_style(s_screen);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_screen, 12, 0);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "ThingSpeak");
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    lv_obj_t *form = create_form_panel(s_screen);

    lv_obj_t *enable_row = lv_obj_create(form);
    lv_obj_remove_style_all(enable_row);
    lv_obj_set_width(enable_row, LV_PCT(100));
    lv_obj_set_height(enable_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enable_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(enable_row, LV_OBJ_FLAG_SCROLLABLE);

    create_field_label(enable_row, "Enable uploads");
    s_enabled_sw = lv_switch_create(enable_row);
    lv_obj_add_event_cb(s_enabled_sw, enabled_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_field_label(form, "API Write Key");
    s_api_key_ta = lv_textarea_create(form);
    style_settings_input(s_api_key_ta);
    lv_obj_set_width(s_api_key_ta, LV_PCT(70));
    lv_obj_set_height(s_api_key_ta, 48);
    lv_textarea_set_one_line(s_api_key_ta, true);
    lv_textarea_set_max_length(s_api_key_ta, AQUAPILOT_THINGSPEAK_KEY_MAX);
    lv_textarea_set_password_mode(s_api_key_ta, true);
    lv_textarea_set_placeholder_text(s_api_key_ta, "16-character write key");
    lv_obj_add_event_cb(s_api_key_ta, api_key_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_api_key_ta, credential_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    create_field_label(form, "Channel ID");
    s_channel_ta = lv_textarea_create(form);
    style_settings_input(s_channel_ta);
    lv_obj_set_width(s_channel_ta, LV_PCT(40));
    lv_obj_set_height(s_channel_ta, 48);
    lv_textarea_set_one_line(s_channel_ta, true);
    lv_textarea_set_max_length(s_channel_ta, AQUAPILOT_THINGSPEAK_CHANNEL_MAX);
    lv_textarea_set_placeholder_text(s_channel_ta, "Channel number");
    lv_obj_add_event_cb(s_channel_ta, channel_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_channel_ta, credential_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    s_status_label = lv_label_create(form);
    lv_label_set_text(s_status_label, thingspeak_uploader_last_status_text());
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_status_label, LV_PCT(100));

    lv_obj_t *section = lv_label_create(form);
    lv_label_set_text(section, "Fields 1-8");
    lv_obj_set_style_text_color(section, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(section, &lv_font_montserrat_20, 0);
    lv_obj_set_width(section, LV_PCT(100));

    create_fields_table(form);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_set_width(s_keyboard, LV_PCT(100));
    lv_obj_set_height(s_keyboard, 180);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    position_keyboard();
    lv_obj_add_event_cb(s_keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_t *back = ui_create_back_button(s_screen, back_cb);
    lv_obj_add_flag(back, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_move_foreground(back);

    thingspeak_refresh_fields();
    s_status_timer = lv_timer_create(status_timer_cb, 2000, NULL);
}

void screen_thingspeak_show(void)
{
    if (s_screen == NULL) {
        return;
    }
    thingspeak_refresh_fields();
    hide_keyboard();
    lv_screen_load(s_screen);
}
