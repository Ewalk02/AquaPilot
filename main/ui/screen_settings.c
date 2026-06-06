#include "screen_settings.h"

#include "lvgl.h"
#include "safety/filter_calibration.h"
#include "safety/filter_power_monitor.h"
#include "screen_wifi.h"
#include "storage/aquapilot_settings.h"
#include "ui_nav.h"

#include "chihiros_heater_protocol.h"
#include "heater/chihiros_ble.h"
#include "heater/heater_service.h"
#include "schedule/aquapilot_time.h"
#include "timezone_options.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCREEN_BG_COLOR   0x0D1117
#define SHELLY_ADDR_PREFIX "192.168.1."
#define PANEL_BG_COLOR    0x161B22
#define BORDER_COLOR      0x30363D
#define TITLE_COLOR       0xE6EDF3
#define LABEL_COLOR       0x8B949E
#define VALUE_COLOR       0xC9D1D9
#define STATUS_COLOR      0x6E7681
#define BTN_BG_COLOR      0x21262D

static lv_obj_t *s_hub_screen;
static lv_obj_t *s_temp_range_screen;
static lv_obj_t *s_co2_screen;
static lv_obj_t *s_filter_screen;
static lv_obj_t *s_shelly_screen;
static lv_obj_t *s_safety_screen;
static lv_obj_t *s_time_screen;

static lv_obj_t *s_temp_setpoint_ta;
static lv_obj_t *s_temp_delta_plus_ta;
static lv_obj_t *s_temp_delta_minus_ta;
static lv_obj_t *s_temp_range_preview;
static lv_obj_t *s_temp_setpoint_status;
static lv_obj_t *s_co2_on_ta;
static lv_obj_t *s_co2_off_ta;
static lv_obj_t *s_filter_status;
static lv_obj_t *s_filter_message;
static lv_obj_t *s_filter_watts_label;
static lv_obj_t *s_filter_avg_label;
static lv_obj_t *s_filter_progress_bar;
static lv_obj_t *s_filter_progress_label;
static lv_obj_t *s_filter_cal_btn;
static lv_obj_t *s_filter_green_band_ta;
static lv_obj_t *s_filter_yellow_band_ta;
static lv_obj_t *s_filter_red_band_ta;
static lv_obj_t *s_filter_band_status;
static lv_timer_t *s_filter_ui_timer;
static lv_obj_t *s_filter_keyboard;
static lv_obj_t *s_shelly_heater_ta;
static lv_obj_t *s_shelly_filter_ta;
static lv_obj_t *s_shelly_co2_ta;
static lv_obj_t *s_shelly_status;
static lv_obj_t *s_heater_override_sw;
static lv_obj_t *s_heater_shelly_power_monitor_sw;
static lv_obj_t *s_co2_power_monitor_sw;
static lv_obj_t *s_wifi_time_sw;
static lv_obj_t *s_tz_dd;
static lv_obj_t *s_manual_date_ta;
static lv_obj_t *s_manual_time_ta;
static lv_obj_t *s_time_preview;
static lv_obj_t *s_time_status;
static lv_obj_t *s_temp_keyboard;
static lv_obj_t *s_co2_keyboard;
static lv_obj_t *s_shelly_keyboard;
static lv_obj_t *s_time_keyboard;

static void shelly_ta_focus_cb(lv_event_t *e);
static bool time_settings_save_from_fields(void);
static void time_settings_refresh_fields(void);

static void hide_keyboard(lv_obj_t *keyboard)
{
    if (keyboard != NULL) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_all_keyboards(void)
{
    hide_keyboard(s_temp_keyboard);
    hide_keyboard(s_co2_keyboard);
    hide_keyboard(s_shelly_keyboard);
    hide_keyboard(s_time_keyboard);
    hide_keyboard(s_filter_keyboard);
}

static const char *const s_time_kb_map[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", ":", "\n",
    LV_SYMBOL_LEFT, "0", LV_SYMBOL_RIGHT, LV_SYMBOL_CLOSE, "",
};

static const lv_buttonmatrix_ctrl_t s_time_kb_ctrl[] = {
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
};

static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *keyboard = lv_event_get_user_data(e);
    if (keyboard != NULL) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_defocus_cb(lv_event_t *e)
{
    (void)e;
    hide_all_keyboards();
}

static void co2_ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *keyboard = lv_event_get_user_data(e);
    if (keyboard != NULL) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void attach_co2_field(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, co2_ta_focus_cb, LV_EVENT_FOCUSED, s_co2_keyboard);
    lv_obj_add_event_cb(ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

static void attach_shelly_field(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, shelly_ta_focus_cb, LV_EVENT_FOCUSED, s_shelly_keyboard);
    lv_obj_add_event_cb(ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

static bool parse_time_hhmm(const char *text, uint8_t *hour, uint8_t *minute)
{
    if (text == NULL || hour == NULL || minute == NULL) {
        return false;
    }

    int h = 0;
    int m = 0;
    if (sscanf(text, "%d:%d", &h, &m) != 2) {
        return false;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }

    *hour = (uint8_t)h;
    *minute = (uint8_t)m;
    return true;
}

static void format_time_hhmm(char *buf, size_t len, uint8_t hour, uint8_t minute)
{
    snprintf(buf, len, "%02u:%02u", (unsigned)hour, (unsigned)minute);
}

static void temp_setpoint_show_status(const char *text)
{
    if (s_temp_setpoint_status != NULL && text != NULL) {
        lv_label_set_text(s_temp_setpoint_status, text);
    }
}

static void temp_range_update_preview(void)
{
    if (s_temp_range_preview == NULL) {
        return;
    }

    float min_f = 0.0f;
    float max_f = 0.0f;
    aquapilot_settings_get_temp_range(&min_f, &max_f);

    char line[48];
    snprintf(line, sizeof(line), "Alert range: %.1f - %.1f F", min_f, max_f);
    lv_label_set_text(s_temp_range_preview, line);
}

static bool temp_settings_save_from_fields(void)
{
    if (s_temp_setpoint_ta == NULL || s_temp_delta_plus_ta == NULL || s_temp_delta_minus_ta == NULL) {
        return false;
    }

    const char *setpoint_txt = lv_textarea_get_text(s_temp_setpoint_ta);
    const char *plus_txt = lv_textarea_get_text(s_temp_delta_plus_ta);
    const char *minus_txt = lv_textarea_get_text(s_temp_delta_minus_ta);

    if (setpoint_txt == NULL || setpoint_txt[0] == '\0') {
        temp_setpoint_show_status("Setpoint is required.");
        return false;
    }

    float setpoint_f = (float)strtod(setpoint_txt, NULL);
    float delta_plus = plus_txt != NULL ? (float)strtod(plus_txt, NULL) : 0.0f;
    float delta_minus = minus_txt != NULL ? (float)strtod(minus_txt, NULL) : 0.0f;

    if (!chihiros_setpoint_f_allowed(setpoint_f)) {
        temp_setpoint_show_status("Setpoint must be between 50 and 95 F.");
        return false;
    }
    if (delta_plus < 0.1f || delta_plus > 25.0f || delta_minus < 0.1f || delta_minus > 25.0f) {
        temp_setpoint_show_status("Deltas must be between 0.1 and 25 F.");
        return false;
    }

    if (!aquapilot_settings_set_temp_deltas(delta_plus, delta_minus) ||
        !aquapilot_settings_set_heater_setpoint(setpoint_f)) {
        temp_setpoint_show_status("Could not save temperature settings.");
        return false;
    }

    heater_service_request_setpoint_apply();
    temp_range_update_preview();

    chihiros_status_t st = {0};
    if (chihiros_ble_get_status(&st) && st.setpoint_f_last_sent == setpoint_f) {
        temp_setpoint_show_status("Settings saved. Setpoint sent to heater.");
    } else {
        temp_setpoint_show_status("Settings saved. Setpoint will apply when heater connects.");
    }
    return true;
}

static void temp_settings_refresh_fields(void)
{
    float setpoint_f = 77.0f;
    float delta_plus = 5.0f;
    float delta_minus = 5.0f;

    if (!aquapilot_settings_get_heater_setpoint(&setpoint_f)) {
        setpoint_f = 77.0f;
    }
    aquapilot_settings_get_temp_deltas(&delta_plus, &delta_minus);

    if (s_temp_setpoint_ta != NULL) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", setpoint_f);
        lv_textarea_set_text(s_temp_setpoint_ta, buf);
    }
    if (s_temp_delta_plus_ta != NULL) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", delta_plus);
        lv_textarea_set_text(s_temp_delta_plus_ta, buf);
    }
    if (s_temp_delta_minus_ta != NULL) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", delta_minus);
        lv_textarea_set_text(s_temp_delta_minus_ta, buf);
    }

    temp_range_update_preview();
    temp_setpoint_show_status("");
}

static void temp_field_defocus_cb(lv_event_t *e)
{
    ta_defocus_cb(e);
    temp_settings_save_from_fields();
}

static void attach_temp_field(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, s_temp_keyboard);
    lv_obj_add_event_cb(ta, temp_field_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

static void co2_save_from_fields(void)
{
    if (s_co2_on_ta == NULL || s_co2_off_ta == NULL) {
        return;
    }

    uint8_t on_h = 7;
    uint8_t on_m = 0;
    uint8_t off_h = 21;
    uint8_t off_m = 0;
    parse_time_hhmm(lv_textarea_get_text(s_co2_on_ta), &on_h, &on_m);
    parse_time_hhmm(lv_textarea_get_text(s_co2_off_ta), &off_h, &off_m);
    aquapilot_settings_set_co2_schedule(on_h, on_m, off_h, off_m);
}

static void co2_refresh_fields(void)
{
    if (s_co2_on_ta == NULL || s_co2_off_ta == NULL) {
        return;
    }

    uint8_t on_h = 7;
    uint8_t on_m = 0;
    uint8_t off_h = 21;
    uint8_t off_m = 0;
    aquapilot_settings_get_co2_schedule(&on_h, &on_m, &off_h, &off_m);

    char on_buf[8];
    char off_buf[8];
    format_time_hhmm(on_buf, sizeof(on_buf), on_h, on_m);
    format_time_hhmm(off_buf, sizeof(off_buf), off_h, off_m);
    lv_textarea_set_text(s_co2_on_ta, on_buf);
    lv_textarea_set_text(s_co2_off_ta, off_buf);
}

static void filter_stop_ui_timer(void)
{
    if (s_filter_ui_timer != NULL) {
        lv_timer_delete(s_filter_ui_timer);
        s_filter_ui_timer = NULL;
    }
}

static void filter_refresh_ui(void)
{
    if (s_filter_message != NULL) {
        lv_label_set_text(s_filter_message, filter_calibration_get_message());
    }

    filter_cal_state_t state = filter_calibration_get_state();
    const bool calibrating = filter_calibration_is_active();

    if (s_filter_progress_bar != NULL) {
        if (state == FILTER_CAL_SAMPLING) {
            lv_obj_remove_flag(s_filter_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_filter_progress_bar, filter_calibration_get_progress_pct(), LV_ANIM_OFF);
        } else {
            lv_obj_add_flag(s_filter_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_filter_progress_bar, 0, LV_ANIM_OFF);
        }
    }

    if (s_filter_progress_label != NULL) {
        if (state == FILTER_CAL_SAMPLING) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Progress: %u%%", (unsigned)filter_calibration_get_progress_pct());
            lv_label_set_text(s_filter_progress_label, buf);
            lv_obj_remove_flag(s_filter_progress_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_filter_progress_label, "");
            lv_obj_add_flag(s_filter_progress_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_filter_watts_label != NULL) {
        uint16_t watts = 0;
        if (filter_calibration_get_current_watts(&watts) || filter_power_monitor_get_watts(&watts)) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Current: %u W", (unsigned)watts);
            lv_label_set_text(s_filter_watts_label, buf);
        } else {
            lv_label_set_text(s_filter_watts_label, "Current: -- W");
        }
    }

    if (s_filter_avg_label != NULL) {
        float avg = 0.0f;
        if (filter_calibration_get_running_average(&avg)) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Average: %.1f W", avg);
            lv_label_set_text(s_filter_avg_label, buf);
            lv_obj_remove_flag(s_filter_avg_label, LV_OBJ_FLAG_HIDDEN);
        } else if (filter_calibration_get_result_baseline(&avg)) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Saved baseline: %.1f W", avg);
            lv_label_set_text(s_filter_avg_label, buf);
            lv_obj_remove_flag(s_filter_avg_label, LV_OBJ_FLAG_HIDDEN);
        } else if (filter_power_monitor_get_baseline_watts(&avg)) {
            char buf[64];
            const char *zone_name = "unknown";
            switch (filter_power_monitor_get_zone()) {
            case FILTER_POWER_ZONE_GREEN:
                zone_name = "normal";
                break;
            case FILTER_POWER_ZONE_YELLOW:
                zone_name = "warning";
                break;
            case FILTER_POWER_ZONE_RED:
                zone_name = "critical";
                break;
            case FILTER_POWER_ZONE_OFF:
                zone_name = "off";
                break;
            default:
                break;
            }
            snprintf(buf, sizeof(buf), "Baseline: %.1f W (%s)", avg, zone_name);
            lv_label_set_text(s_filter_avg_label, buf);
            lv_obj_remove_flag(s_filter_avg_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_filter_avg_label, "");
            lv_obj_add_flag(s_filter_avg_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_filter_cal_btn != NULL) {
        lv_obj_t *btn_lbl = lv_obj_get_child(s_filter_cal_btn, 0);
        if (calibrating) {
            if (btn_lbl != NULL) {
                lv_label_set_text(btn_lbl, "Cancel");
            }
            lv_obj_remove_state(s_filter_cal_btn, LV_STATE_DISABLED);
        } else {
            if (btn_lbl != NULL) {
                lv_label_set_text(btn_lbl, "Start Calibration");
            }
            lv_obj_remove_state(s_filter_cal_btn, LV_STATE_DISABLED);
        }
    }

    if (s_filter_status != NULL) {
        if (state == FILTER_CAL_COMPLETE) {
            float baseline = 0.0f;
            if (filter_calibration_get_result_baseline(&baseline)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Saved baseline: %.1f W", baseline);
                lv_label_set_text(s_filter_status, buf);
            } else {
                lv_label_set_text(s_filter_status, "Calibration saved.");
            }
        } else if (state == FILTER_CAL_FAILED) {
            lv_label_set_text(s_filter_status, "Calibration failed. Try again.");
        } else if (!calibrating && filter_power_monitor_is_calibrated()) {
            lv_label_set_text(s_filter_status, "Filter calibrated. Power is compared to the baseline.");
        } else if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
            lv_label_set_text(s_filter_status, "Set the filter Shelly address first.");
        } else {
            lv_label_set_text(s_filter_status, "");
        }
    }
}

static void filter_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    filter_refresh_ui();
}

static void filter_start_ui_timer(void)
{
    if (s_filter_ui_timer == NULL) {
        s_filter_ui_timer = lv_timer_create(filter_ui_timer_cb, FILTER_CALIBRATION_POLL_MS, NULL);
    }
}

static void filter_refresh_status(void)
{
    filter_refresh_ui();
}

static void filter_band_show_status(const char *text)
{
    if (s_filter_band_status != NULL && text != NULL) {
        lv_label_set_text(s_filter_band_status, text);
    }
}

static void filter_bands_refresh_fields(void)
{
    if (s_filter_green_band_ta == NULL || s_filter_yellow_band_ta == NULL || s_filter_red_band_ta == NULL) {
        return;
    }

    uint8_t green_pct = 10;
    uint8_t yellow_pct = 25;
    uint8_t red_pct = 40;
    aquapilot_settings_get_filter_bands(&green_pct, &yellow_pct, &red_pct);

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)green_pct);
    lv_textarea_set_text(s_filter_green_band_ta, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)yellow_pct);
    lv_textarea_set_text(s_filter_yellow_band_ta, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)red_pct);
    lv_textarea_set_text(s_filter_red_band_ta, buf);
}

static bool filter_bands_save_from_fields(void)
{
    if (s_filter_green_band_ta == NULL || s_filter_yellow_band_ta == NULL || s_filter_red_band_ta == NULL) {
        return false;
    }

    const int green = atoi(lv_textarea_get_text(s_filter_green_band_ta));
    const int yellow = atoi(lv_textarea_get_text(s_filter_yellow_band_ta));
    const int red = atoi(lv_textarea_get_text(s_filter_red_band_ta));

    if (green < 1 || yellow <= green || red <= yellow || red > 100) {
        filter_band_show_status("Bands must increase: green < yellow < red (max 100%).");
        return false;
    }

    if (!aquapilot_settings_set_filter_bands((uint8_t)green, (uint8_t)yellow, (uint8_t)red)) {
        filter_band_show_status("Invalid band percentages.");
        return false;
    }

    filter_band_show_status("Gauge bands saved.");
    return true;
}

static void filter_band_field_defocus_cb(lv_event_t *e)
{
    ta_defocus_cb(e);
    filter_bands_save_from_fields();
}

static void attach_filter_band_field(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, s_filter_keyboard);
    lv_obj_add_event_cb(ta, filter_band_field_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

static void filter_back_cb(lv_event_t *e)
{
    (void)e;
    filter_calibration_cancel();
    filter_bands_save_from_fields();
    filter_stop_ui_timer();
    hide_all_keyboards();
    if (s_hub_screen != NULL) {
        lv_screen_load(s_hub_screen);
    }
}

static void shelly_show_status(const char *text)
{
    if (s_shelly_status != NULL && text != NULL) {
        lv_label_set_text(s_shelly_status, text);
    }
}

static bool shelly_address_is_unconfigured(const char *text)
{
    return text == NULL || text[0] == '\0' || strcmp(text, SHELLY_ADDR_PREFIX) == 0;
}

static const char *shelly_text_for_save(const char *text)
{
    return shelly_address_is_unconfigured(text) ? "" : text;
}

static void shelly_set_field_text(lv_obj_t *ta, const char *stored)
{
    if (ta == NULL) {
        return;
    }

    if (shelly_address_is_unconfigured(stored)) {
        lv_textarea_set_text(ta, SHELLY_ADDR_PREFIX);
    } else {
        lv_textarea_set_text(ta, stored);
    }
}

static void shelly_ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *keyboard = lv_event_get_user_data(e);

    if (keyboard != NULL) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    const char *text = lv_textarea_get_text(ta);
    if (shelly_address_is_unconfigured(text)) {
        lv_textarea_set_text(ta, SHELLY_ADDR_PREFIX);
    }
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
}

static bool shelly_save_from_fields(void)
{
    if (s_shelly_heater_ta == NULL || s_shelly_filter_ta == NULL || s_shelly_co2_ta == NULL) {
        return false;
    }

    const char *heater_txt = lv_textarea_get_text(s_shelly_heater_ta);
    const char *filter_txt = lv_textarea_get_text(s_shelly_filter_ta);
    const char *co2_txt = lv_textarea_get_text(s_shelly_co2_ta);

    if (!aquapilot_settings_set_shelly_addresses(shelly_text_for_save(heater_txt),
                                                 shelly_text_for_save(filter_txt),
                                                 shelly_text_for_save(co2_txt))) {
        shelly_show_status("Use hostnames or IPs (letters, numbers, . - : only).");
        return false;
    }

    shelly_show_status("Shelly addresses saved.");
    return true;
}

static void shelly_refresh_fields(void)
{
    char buf[AQUAPILOT_SHELLY_ADDR_MAX];

    if (s_shelly_heater_ta != NULL) {
        aquapilot_settings_get_shelly_address(AQUAPILOT_SHELLY_HEATER, buf, sizeof(buf));
        shelly_set_field_text(s_shelly_heater_ta, buf);
    }
    if (s_shelly_filter_ta != NULL) {
        aquapilot_settings_get_shelly_address(AQUAPILOT_SHELLY_FILTER, buf, sizeof(buf));
        shelly_set_field_text(s_shelly_filter_ta, buf);
    }
    if (s_shelly_co2_ta != NULL) {
        aquapilot_settings_get_shelly_address(AQUAPILOT_SHELLY_CO2, buf, sizeof(buf));
        shelly_set_field_text(s_shelly_co2_ta, buf);
    }

    shelly_show_status("");
}

static void settings_keyboard_accept(lv_obj_t *ta)
{
    if (ta == NULL) {
        return;
    }

    if (ta == s_co2_on_ta || ta == s_co2_off_ta) {
        co2_save_from_fields();
    } else if (ta == s_temp_setpoint_ta || ta == s_temp_delta_plus_ta || ta == s_temp_delta_minus_ta) {
        temp_settings_save_from_fields();
    } else if (ta == s_shelly_heater_ta || ta == s_shelly_filter_ta || ta == s_shelly_co2_ta) {
        shelly_save_from_fields();
    } else if (ta == s_manual_date_ta || ta == s_manual_time_ta) {
        time_settings_save_from_fields();
    }
}

static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);

    if (ta != NULL) {
        settings_keyboard_accept(ta);
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
    hide_keyboard(kb);
}

static void keyboard_cancel_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);

    hide_keyboard(kb);
    if (ta != NULL) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

static void bind_keyboard(lv_obj_t *kb)
{
    lv_obj_add_event_cb(kb, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);
}

static void safety_refresh_fields(void)
{
    bool enabled = false;
    aquapilot_settings_get_heater_override_enabled(&enabled);

    if (s_heater_override_sw != NULL) {
        if (enabled) {
            lv_obj_add_state(s_heater_override_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_heater_override_sw, LV_STATE_CHECKED);
        }
    }

    bool co2_monitor = false;
    aquapilot_settings_get_co2_power_monitor_enabled(&co2_monitor);

    if (s_co2_power_monitor_sw != NULL) {
        if (co2_monitor) {
            lv_obj_add_state(s_co2_power_monitor_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_co2_power_monitor_sw, LV_STATE_CHECKED);
        }
    }

    bool heater_shelly_monitor = false;
    aquapilot_settings_get_heater_shelly_power_monitor_enabled(&heater_shelly_monitor);

    if (s_heater_shelly_power_monitor_sw != NULL) {
        if (heater_shelly_monitor) {
            lv_obj_add_state(s_heater_shelly_power_monitor_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_heater_shelly_power_monitor_sw, LV_STATE_CHECKED);
        }
    }
}

static void heater_override_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    aquapilot_settings_set_heater_override_enabled(enabled);
}

static void co2_power_monitor_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    aquapilot_settings_set_co2_power_monitor_enabled(enabled);
}

static void heater_shelly_power_monitor_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    aquapilot_settings_set_heater_shelly_power_monitor_enabled(enabled);
}

static void apply_screen_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
}

static void style_menu_button(lv_obj_t *btn)
{
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(BTN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
}

static lv_obj_t *create_screen_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    return title;
}

static lv_obj_t *create_back_button(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 140, 48);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Back");
    lv_obj_center(lbl);
    return btn;
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
    lv_obj_set_style_pad_row(form, 12, 0);
    return form;
}

static lv_obj_t *create_field_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    return lbl;
}

static lv_obj_t *create_one_line_field(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 48);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 8);
    lv_textarea_set_placeholder_text(ta, placeholder);
    return ta;
}

static lv_obj_t *create_timezone_dropdown(lv_obj_t *parent)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, LV_PCT(100));
    lv_dropdown_set_options_static(dd, timezone_options_dropdown_string());
    lv_obj_set_style_bg_color(dd, lv_color_hex(BTN_BG_COLOR), 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_text_color(dd, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_16, 0);
    return dd;
}

static lv_obj_t *create_text_field(lv_obj_t *parent, const char *placeholder, size_t max_len)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 48);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, (uint32_t)max_len);
    lv_textarea_set_placeholder_text(ta, placeholder);
    return ta;
}

static lv_obj_t *create_address_field(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 48);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, AQUAPILOT_SHELLY_ADDR_MAX - 1);
    lv_textarea_set_placeholder_text(ta, placeholder);
    return ta;
}

static void hub_back_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_show_dashboard();
}

static void temp_settings_back_cb(lv_event_t *e)
{
    (void)e;
    hide_all_keyboards();
    temp_settings_save_from_fields();
    screen_settings_show();
}

static void co2_back_cb(lv_event_t *e)
{
    (void)e;
    hide_all_keyboards();
    co2_save_from_fields();
    screen_settings_show();
}

static void sub_back_cb(lv_event_t *e)
{
    (void)e;
    hide_all_keyboards();
    screen_settings_show();
}

static void shelly_back_cb(lv_event_t *e)
{
    (void)e;
    hide_all_keyboards();
    shelly_save_from_fields();
    screen_settings_show();
}

static void menu_wifi_cb(lv_event_t *e)
{
    (void)e;
    screen_wifi_show();
}

static void menu_temp_settings_cb(lv_event_t *e)
{
    (void)e;
    if (s_temp_range_screen != NULL) {
        hide_all_keyboards();
        temp_settings_refresh_fields();
        lv_screen_load(s_temp_range_screen);
    }
}

static void menu_co2_cb(lv_event_t *e)
{
    (void)e;
    if (s_co2_screen != NULL) {
        hide_all_keyboards();
        co2_refresh_fields();
        lv_screen_load(s_co2_screen);
    }
}

static void menu_filter_cb(lv_event_t *e)
{
    (void)e;
    if (s_filter_screen != NULL) {
        hide_all_keyboards();
        filter_bands_refresh_fields();
        filter_refresh_status();
        filter_start_ui_timer();
        lv_screen_load(s_filter_screen);
    }
}

static void menu_shelly_cb(lv_event_t *e)
{
    (void)e;
    if (s_shelly_screen != NULL) {
        hide_all_keyboards();
        shelly_refresh_fields();
        lv_screen_load(s_shelly_screen);
    }
}

static void menu_safety_cb(lv_event_t *e)
{
    (void)e;
    if (s_safety_screen != NULL) {
        hide_all_keyboards();
        safety_refresh_fields();
        lv_screen_load(s_safety_screen);
    }
}

static void menu_time_cb(lv_event_t *e)
{
    (void)e;
    if (s_time_screen != NULL) {
        hide_all_keyboards();
        time_settings_refresh_fields();
        lv_screen_load(s_time_screen);
    }
}

static lv_obj_t *create_menu_button(lv_obj_t *parent, const char *label, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    style_menu_button(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    return btn;
}

static void create_hub_screen(void)
{
    s_hub_screen = lv_obj_create(NULL);
    apply_screen_style(s_hub_screen);
    lv_obj_set_flex_flow(s_hub_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_hub_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_hub_screen, 16, 0);

    create_screen_title(s_hub_screen, "Settings");

    lv_obj_t *menu = lv_obj_create(s_hub_screen);
    lv_obj_remove_style_all(menu);
    lv_obj_set_width(menu, LV_PCT(100));
    lv_obj_set_height(menu, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(menu, 10, 0);

    create_menu_button(menu, "Wi-Fi Configuration", menu_wifi_cb);
    create_menu_button(menu, "Time Settings", menu_time_cb);
    create_menu_button(menu, "Tank Temperature Settings", menu_temp_settings_cb);
    create_menu_button(menu, "Shelly Configuration", menu_shelly_cb);
    create_menu_button(menu, "Safety Settings", menu_safety_cb);
    create_menu_button(menu, "CO2 Schedule", menu_co2_cb);
    create_menu_button(menu, "Filter Calibration", menu_filter_cb);

    create_back_button(s_hub_screen, hub_back_cb);
}

static void create_temp_range_screen(void)
{
    s_temp_range_screen = lv_obj_create(NULL);
    apply_screen_style(s_temp_range_screen);
    lv_obj_set_flex_flow(s_temp_range_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_temp_range_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_temp_range_screen, 12, 0);

    create_screen_title(s_temp_range_screen, "Tank Temperature Settings");

    lv_obj_t *hint = lv_label_create(s_temp_range_screen);
    lv_label_set_text(hint, "Setpoint and deltas define the dashboard alert range and heater target.");
    lv_obj_set_style_text_color(hint, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_width(hint, LV_PCT(100));

    lv_obj_t *form = create_form_panel(s_temp_range_screen);
    create_field_label(form, "Heater setpoint (°F)");
    s_temp_setpoint_ta = create_one_line_field(form, "77.0");

    create_field_label(form, "Temp Delta + (°F above setpoint)");
    s_temp_delta_plus_ta = create_one_line_field(form, "5.0");

    create_field_label(form, "Temp Delta - (°F below setpoint)");
    s_temp_delta_minus_ta = create_one_line_field(form, "5.0");

    s_temp_range_preview = lv_label_create(form);
    lv_label_set_text(s_temp_range_preview, "Alert range: --");
    lv_obj_set_style_text_color(s_temp_range_preview, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(s_temp_range_preview, &lv_font_montserrat_20, 0);
    lv_obj_set_width(s_temp_range_preview, LV_PCT(100));

    s_temp_setpoint_status = lv_label_create(form);
    lv_label_set_text(s_temp_setpoint_status, "");
    lv_obj_set_style_text_color(s_temp_setpoint_status, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_temp_setpoint_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_temp_setpoint_status, LV_PCT(100));

    s_temp_keyboard = lv_keyboard_create(s_temp_range_screen);
    lv_obj_set_width(s_temp_keyboard, LV_PCT(100));
    lv_obj_set_height(s_temp_keyboard, 180);
    lv_obj_add_flag(s_temp_keyboard, LV_OBJ_FLAG_HIDDEN);
    bind_keyboard(s_temp_keyboard);

    attach_temp_field(s_temp_setpoint_ta);
    attach_temp_field(s_temp_delta_plus_ta);
    attach_temp_field(s_temp_delta_minus_ta);
    temp_settings_refresh_fields();

    create_back_button(s_temp_range_screen, temp_settings_back_cb);
}

static void create_co2_screen(void)
{
    s_co2_screen = lv_obj_create(NULL);
    apply_screen_style(s_co2_screen);
    lv_obj_set_flex_flow(s_co2_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_co2_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_co2_screen, 12, 0);

    create_screen_title(s_co2_screen, "CO2 Schedule");

    lv_obj_t *form = create_form_panel(s_co2_screen);

    create_field_label(form, "Injection on (HH:MM, 24h)");
    s_co2_on_ta = create_one_line_field(form, "07:00");
    lv_textarea_set_max_length(s_co2_on_ta, 5);

    create_field_label(form, "Injection off (HH:MM, 24h)");
    s_co2_off_ta = create_one_line_field(form, "21:00");
    lv_textarea_set_max_length(s_co2_off_ta, 5);

    s_co2_keyboard = lv_keyboard_create(s_co2_screen);
    lv_obj_set_width(s_co2_keyboard, LV_PCT(100));
    lv_obj_set_height(s_co2_keyboard, 180);
    lv_obj_add_flag(s_co2_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_map(s_co2_keyboard, LV_KEYBOARD_MODE_USER_1, s_time_kb_map, s_time_kb_ctrl);
    bind_keyboard(s_co2_keyboard);

    attach_co2_field(s_co2_on_ta);
    attach_co2_field(s_co2_off_ta);
    co2_refresh_fields();

    create_back_button(s_co2_screen, co2_back_cb);
}

static void filter_calibrate_cb(lv_event_t *e)
{
    (void)e;

    if (filter_calibration_is_active()) {
        filter_calibration_cancel();
        filter_refresh_ui();
        return;
    }

    if (!aquapilot_settings_has_shelly_address(AQUAPILOT_SHELLY_FILTER)) {
        if (s_filter_status != NULL) {
            lv_label_set_text(s_filter_status, "Set the filter Shelly address first.");
        }
        return;
    }

    filter_calibration_start();
    filter_start_ui_timer();
    filter_refresh_ui();
}

static void create_filter_screen(void)
{
    s_filter_screen = lv_obj_create(NULL);
    apply_screen_style(s_filter_screen);
    lv_obj_set_flex_flow(s_filter_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_filter_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_filter_screen, 12, 0);

    create_screen_title(s_filter_screen, "Filter Calibration");

    lv_obj_t *form = create_form_panel(s_filter_screen);

    lv_obj_t *steps = lv_label_create(form);
    lv_label_set_text(steps,
                      "1. Clean and purge the filter.\n"
                      "2. Turn the filter on at the Shelly plug.\n"
                      "3. Tap Start Calibration to measure 30 seconds of power draw.");
    lv_obj_set_style_text_color(steps, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(steps, &lv_font_montserrat_16, 0);
    lv_obj_set_width(steps, LV_PCT(100));

    s_filter_message = lv_label_create(form);
    lv_label_set_text(s_filter_message, filter_calibration_get_message());
    lv_obj_set_style_text_color(s_filter_message, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(s_filter_message, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_filter_message, LV_PCT(100));

    s_filter_watts_label = lv_label_create(form);
    lv_label_set_text(s_filter_watts_label, "Current: -- W");
    lv_obj_set_style_text_color(s_filter_watts_label, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(s_filter_watts_label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(s_filter_watts_label, LV_PCT(100));

    s_filter_avg_label = lv_label_create(form);
    lv_label_set_text(s_filter_avg_label, "");
    lv_obj_set_style_text_color(s_filter_avg_label, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(s_filter_avg_label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(s_filter_avg_label, LV_PCT(100));
    lv_obj_add_flag(s_filter_avg_label, LV_OBJ_FLAG_HIDDEN);

    s_filter_progress_bar = lv_bar_create(form);
    lv_obj_set_width(s_filter_progress_bar, LV_PCT(100));
    lv_obj_set_height(s_filter_progress_bar, 20);
    lv_bar_set_range(s_filter_progress_bar, 0, 100);
    lv_obj_add_flag(s_filter_progress_bar, LV_OBJ_FLAG_HIDDEN);

    s_filter_progress_label = lv_label_create(form);
    lv_label_set_text(s_filter_progress_label, "");
    lv_obj_set_style_text_color(s_filter_progress_label, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_filter_progress_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_filter_progress_label, LV_PCT(100));
    lv_obj_add_flag(s_filter_progress_label, LV_OBJ_FLAG_HIDDEN);

    s_filter_cal_btn = lv_button_create(form);
    lv_obj_set_size(s_filter_cal_btn, 240, 48);
    lv_obj_add_event_cb(s_filter_cal_btn, filter_calibrate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(s_filter_cal_btn);
    lv_label_set_text(btn_lbl, "Start Calibration");
    lv_obj_center(btn_lbl);

    s_filter_status = lv_label_create(form);
    lv_label_set_text(s_filter_status, "");
    lv_obj_set_style_text_color(s_filter_status, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_filter_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_filter_status, LV_PCT(100));

    lv_obj_t *band_hint = lv_label_create(form);
    lv_label_set_text(band_hint,
                      "Gauge bands (% of calibrated baseline). Yellow must be wider than green; red widest.");
    lv_obj_set_style_text_color(band_hint, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(band_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_width(band_hint, LV_PCT(100));

    create_field_label(form, "Green band +/- (%)");
    s_filter_green_band_ta = create_one_line_field(form, "10");

    create_field_label(form, "Yellow band +/- (%)");
    s_filter_yellow_band_ta = create_one_line_field(form, "25");

    create_field_label(form, "Red band +/- (%)");
    s_filter_red_band_ta = create_one_line_field(form, "40");

    s_filter_band_status = lv_label_create(form);
    lv_label_set_text(s_filter_band_status, "");
    lv_obj_set_style_text_color(s_filter_band_status, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_filter_band_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_filter_band_status, LV_PCT(100));

    s_filter_keyboard = lv_keyboard_create(s_filter_screen);
    lv_obj_set_width(s_filter_keyboard, LV_PCT(100));
    lv_obj_set_height(s_filter_keyboard, 180);
    lv_obj_add_flag(s_filter_keyboard, LV_OBJ_FLAG_HIDDEN);
    bind_keyboard(s_filter_keyboard);

    attach_filter_band_field(s_filter_green_band_ta);
    attach_filter_band_field(s_filter_yellow_band_ta);
    attach_filter_band_field(s_filter_red_band_ta);
    filter_bands_refresh_fields();

    create_back_button(s_filter_screen, filter_back_cb);
    filter_refresh_status();
}

static void create_shelly_screen(void)
{
    s_shelly_screen = lv_obj_create(NULL);
    apply_screen_style(s_shelly_screen);
    lv_obj_set_flex_flow(s_shelly_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_shelly_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_shelly_screen, 12, 0);

    create_screen_title(s_shelly_screen, "Shelly Configuration");

    lv_obj_t *hint = lv_label_create(s_shelly_screen);
    lv_label_set_text(hint, "Enter the last octet for each plug (e.g. 192.168.1.10). Hostnames are also supported.");
    lv_obj_set_style_text_color(hint, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_width(hint, LV_PCT(100));

    lv_obj_t *form = create_form_panel(s_shelly_screen);

    create_field_label(form, "Heater plug address");
    s_shelly_heater_ta = create_address_field(form, "10");

    create_field_label(form, "Filter plug address");
    s_shelly_filter_ta = create_address_field(form, "11");

    create_field_label(form, "CO2 plug address");
    s_shelly_co2_ta = create_address_field(form, "12");

    s_shelly_status = lv_label_create(form);
    lv_label_set_text(s_shelly_status, "");
    lv_obj_set_style_text_color(s_shelly_status, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_shelly_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_shelly_status, LV_PCT(100));

    s_shelly_keyboard = lv_keyboard_create(s_shelly_screen);
    lv_obj_set_width(s_shelly_keyboard, LV_PCT(100));
    lv_obj_set_height(s_shelly_keyboard, 180);
    lv_obj_add_flag(s_shelly_keyboard, LV_OBJ_FLAG_HIDDEN);
    bind_keyboard(s_shelly_keyboard);

    attach_shelly_field(s_shelly_heater_ta);
    attach_shelly_field(s_shelly_filter_ta);
    attach_shelly_field(s_shelly_co2_ta);
    shelly_refresh_fields();

    create_back_button(s_shelly_screen, shelly_back_cb);
}

static void create_safety_screen(void)
{
    s_safety_screen = lv_obj_create(NULL);
    apply_screen_style(s_safety_screen);
    lv_obj_set_flex_flow(s_safety_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_safety_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_safety_screen, 12, 0);

    create_screen_title(s_safety_screen, "Safety Settings");

    lv_obj_t *form = create_form_panel(s_safety_screen);

    lv_obj_t *section = lv_label_create(form);
    lv_label_set_text(section, "Heater Override");
    lv_obj_set_style_text_color(section, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(section, &lv_font_montserrat_20, 0);
    lv_obj_set_width(section, LV_PCT(100));

    lv_obj_t *desc = lv_label_create(form);
    lv_label_set_text(desc,
                      "When enabled, turns off the heater Shelly plug if tank temperature is above the "
                      "alert range and the heater is drawing more than 5 W, or if the Chihiros heater is "
                      "off but the Shelly plug is still drawing power.");
    lv_obj_set_style_text_color(desc, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_obj_set_width(desc, LV_PCT(100));

    lv_obj_t *toggle_row = lv_obj_create(form);
    lv_obj_remove_style_all(toggle_row);
    lv_obj_set_width(toggle_row, LV_PCT(100));
    lv_obj_set_height(toggle_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(toggle_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggle_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(toggle_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *enable_lbl = lv_label_create(toggle_row);
    lv_label_set_text(enable_lbl, "Enable");
    lv_obj_set_style_text_color(enable_lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(enable_lbl, &lv_font_montserrat_16, 0);

    s_heater_override_sw = lv_switch_create(toggle_row);
    lv_obj_add_event_cb(s_heater_override_sw, heater_override_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *heater_shelly_section = lv_label_create(form);
    lv_label_set_text(heater_shelly_section, "Heater Plug Power Monitor");
    lv_obj_set_style_text_color(heater_shelly_section, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(heater_shelly_section, &lv_font_montserrat_20, 0);
    lv_obj_set_width(heater_shelly_section, LV_PCT(100));

    lv_obj_t *heater_shelly_desc = lv_label_create(form);
    lv_label_set_text(heater_shelly_desc,
                      "When enabled, alerts and turns off the heater Shelly plug if the Chihiros heater "
                      "has shut down but the plug is still drawing more than 5 W.");
    lv_obj_set_style_text_color(heater_shelly_desc, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(heater_shelly_desc, &lv_font_montserrat_16, 0);
    lv_obj_set_width(heater_shelly_desc, LV_PCT(100));

    lv_obj_t *heater_shelly_toggle_row = lv_obj_create(form);
    lv_obj_remove_style_all(heater_shelly_toggle_row);
    lv_obj_set_width(heater_shelly_toggle_row, LV_PCT(100));
    lv_obj_set_height(heater_shelly_toggle_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heater_shelly_toggle_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heater_shelly_toggle_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(heater_shelly_toggle_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heater_shelly_enable_lbl = lv_label_create(heater_shelly_toggle_row);
    lv_label_set_text(heater_shelly_enable_lbl, "Enable");
    lv_obj_set_style_text_color(heater_shelly_enable_lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(heater_shelly_enable_lbl, &lv_font_montserrat_16, 0);

    s_heater_shelly_power_monitor_sw = lv_switch_create(heater_shelly_toggle_row);
    lv_obj_add_event_cb(s_heater_shelly_power_monitor_sw, heater_shelly_power_monitor_switch_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *co2_section = lv_label_create(form);
    lv_label_set_text(co2_section, "CO2 Power Monitor");
    lv_obj_set_style_text_color(co2_section, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(co2_section, &lv_font_montserrat_20, 0);
    lv_obj_set_width(co2_section, LV_PCT(100));

    lv_obj_t *co2_desc = lv_label_create(form);
    lv_label_set_text(co2_desc,
                      "When enabled, alerts if CO2 is scheduled on but the CO2 Shelly plug is drawing "
                      "no power.");
    lv_obj_set_style_text_color(co2_desc, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(co2_desc, &lv_font_montserrat_16, 0);
    lv_obj_set_width(co2_desc, LV_PCT(100));

    lv_obj_t *co2_toggle_row = lv_obj_create(form);
    lv_obj_remove_style_all(co2_toggle_row);
    lv_obj_set_width(co2_toggle_row, LV_PCT(100));
    lv_obj_set_height(co2_toggle_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(co2_toggle_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(co2_toggle_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(co2_toggle_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *co2_enable_lbl = lv_label_create(co2_toggle_row);
    lv_label_set_text(co2_enable_lbl, "Enable");
    lv_obj_set_style_text_color(co2_enable_lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(co2_enable_lbl, &lv_font_montserrat_16, 0);

    s_co2_power_monitor_sw = lv_switch_create(co2_toggle_row);
    lv_obj_add_event_cb(s_co2_power_monitor_sw, co2_power_monitor_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    safety_refresh_fields();
    create_back_button(s_safety_screen, sub_back_cb);
}

static void time_show_status(const char *text)
{
    if (s_time_status != NULL && text != NULL) {
        lv_label_set_text(s_time_status, text);
    }
}

static void time_update_manual_enabled(bool wifi_time)
{
    if (s_manual_date_ta != NULL) {
        if (wifi_time) {
            lv_obj_add_state(s_manual_date_ta, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_manual_date_ta, LV_STATE_DISABLED);
        }
    }
    if (s_manual_time_ta != NULL) {
        if (wifi_time) {
            lv_obj_add_state(s_manual_time_ta, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_manual_time_ta, LV_STATE_DISABLED);
        }
    }
}

static bool parse_date_ymd(const char *text, int *year, int *month, int *day)
{
    if (text == NULL || year == NULL || month == NULL || day == NULL) {
        return false;
    }

    return sscanf(text, "%d-%d-%d", year, month, day) == 3;
}

static bool time_settings_save_from_fields(void)
{
    if (s_tz_dd == NULL || s_wifi_time_sw == NULL) {
        return false;
    }

    const uint32_t tz_index = lv_dropdown_get_selected(s_tz_dd);
    const char *tz_txt = timezone_options_posix(tz_index);
    if (tz_txt == NULL || tz_txt[0] == '\0' || !aquapilot_settings_set_timezone(tz_txt)) {
        time_show_status("Could not save timezone.");
        return false;
    }

    const bool wifi_time = lv_obj_has_state(s_wifi_time_sw, LV_STATE_CHECKED);
    if (!aquapilot_settings_set_wifi_time_enabled(wifi_time)) {
        time_show_status("Could not save time source.");
        return false;
    }

    if (!wifi_time) {
        if (s_manual_date_ta == NULL || s_manual_time_ta == NULL) {
            return false;
        }

        int year = 0;
        int month = 0;
        int day = 0;
        uint8_t hour = 0;
        uint8_t minute = 0;
        if (!parse_date_ymd(lv_textarea_get_text(s_manual_date_ta), &year, &month, &day) ||
            !parse_time_hhmm(lv_textarea_get_text(s_manual_time_ta), &hour, &minute)) {
            time_show_status("Manual date/time must be YYYY-MM-DD and HH:MM.");
            return false;
        }

        if (!aquapilot_time_set_manual_from_local(year, month, day, hour, minute)) {
            time_show_status("Could not apply manual time.");
            return false;
        }
    }

    aquapilot_time_apply_settings();
    time_settings_refresh_fields();
    time_show_status("Time settings saved.");
    return true;
}

static void time_settings_refresh_fields(void)
{
    bool wifi_time = true;
    aquapilot_settings_get_wifi_time_enabled(&wifi_time);

    if (s_wifi_time_sw != NULL) {
        if (wifi_time) {
            lv_obj_add_state(s_wifi_time_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_wifi_time_sw, LV_STATE_CHECKED);
        }
    }

    char tz[AQUAPILOT_TIMEZONE_MAX];
    aquapilot_settings_get_timezone(tz, sizeof(tz));
    if (s_tz_dd != NULL) {
        const int tz_index = timezone_options_find_index(tz);
        lv_dropdown_set_selected(s_tz_dd, tz_index >= 0 ? (uint32_t)tz_index : 0);
    }

    time_t source = time(NULL);
    bool manual_valid = false;
    int64_t manual_epoch = 0;
    if (!wifi_time && aquapilot_settings_get_manual_time_valid(&manual_valid) && manual_valid &&
        aquapilot_settings_get_manual_epoch(&manual_epoch)) {
        source = (time_t)manual_epoch;
    }

    struct tm local = {0};
    if (localtime_r(&source, &local) != NULL) {
        if (s_manual_date_ta != NULL) {
            char date_buf[32];
            snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1,
                     local.tm_mday);
            lv_textarea_set_text(s_manual_date_ta, date_buf);
        }
        if (s_manual_time_ta != NULL) {
            char time_buf[8];
            format_time_hhmm(time_buf, sizeof(time_buf), (uint8_t)local.tm_hour, (uint8_t)local.tm_min);
            lv_textarea_set_text(s_manual_time_ta, time_buf);
        }
    }

    if (s_time_preview != NULL) {
        char preview[32];
        aquapilot_time_format_current(preview, sizeof(preview));
        lv_label_set_text(s_time_preview, preview);
    }

    time_update_manual_enabled(wifi_time);
    time_show_status("");
}

static void wifi_time_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool wifi_time = lv_obj_has_state(sw, LV_STATE_CHECKED);
    aquapilot_settings_set_wifi_time_enabled(wifi_time);
    time_update_manual_enabled(wifi_time);
    aquapilot_time_apply_settings();
    time_settings_refresh_fields();
}

static void time_text_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (s_time_keyboard != NULL) {
        lv_keyboard_set_mode(s_time_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_time_keyboard, ta);
        lv_obj_clear_flag(s_time_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void manual_time_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (s_time_keyboard != NULL) {
        lv_keyboard_set_mode(s_time_keyboard, LV_KEYBOARD_MODE_USER_1);
        lv_keyboard_set_textarea(s_time_keyboard, ta);
        lv_obj_clear_flag(s_time_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void attach_time_text_field(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, time_text_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

static void attach_manual_time_field(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, manual_time_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
}

static void time_settings_back_cb(lv_event_t *e)
{
    (void)e;
    hide_all_keyboards();
    time_settings_save_from_fields();
    screen_settings_show();
}

static void create_time_screen(void)
{
    s_time_screen = lv_obj_create(NULL);
    apply_screen_style(s_time_screen);
    lv_obj_set_flex_flow(s_time_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_time_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_time_screen, 12, 0);

    create_screen_title(s_time_screen, "Time Settings");

    lv_obj_t *hint = lv_label_create(s_time_screen);
    lv_label_set_text(hint, "Use Wi-Fi time for automatic NTP sync, or enter manual date and time.");
    lv_obj_set_style_text_color(hint, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_width(hint, LV_PCT(100));

    lv_obj_t *form = create_form_panel(s_time_screen);

    lv_obj_t *wifi_row = lv_obj_create(form);
    lv_obj_remove_style_all(wifi_row);
    lv_obj_set_width(wifi_row, LV_PCT(100));
    lv_obj_set_height(wifi_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(wifi_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi_lbl = lv_label_create(wifi_row);
    lv_label_set_text(wifi_lbl, "Use Wi-Fi time");
    lv_obj_set_style_text_color(wifi_lbl, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_16, 0);

    s_wifi_time_sw = lv_switch_create(wifi_row);
    lv_obj_add_event_cb(s_wifi_time_sw, wifi_time_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_field_label(form, "Timezone");
    s_tz_dd = create_timezone_dropdown(form);

    create_field_label(form, "Manual date (YYYY-MM-DD)");
    s_manual_date_ta = create_text_field(form, "2026-06-05", 10);

    create_field_label(form, "Manual time (HH:MM, 24h)");
    s_manual_time_ta = create_text_field(form, "12:00", 5);

    s_time_preview = lv_label_create(form);
    lv_label_set_text(s_time_preview, "Clock not set");
    lv_obj_set_style_text_color(s_time_preview, lv_color_hex(VALUE_COLOR), 0);
    lv_obj_set_style_text_font(s_time_preview, &lv_font_montserrat_20, 0);
    lv_obj_set_width(s_time_preview, LV_PCT(100));

    s_time_status = lv_label_create(form);
    lv_label_set_text(s_time_status, "");
    lv_obj_set_style_text_color(s_time_status, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_time_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_time_status, LV_PCT(100));

    s_time_keyboard = lv_keyboard_create(s_time_screen);
    lv_obj_set_width(s_time_keyboard, LV_PCT(100));
    lv_obj_set_height(s_time_keyboard, 180);
    lv_obj_add_flag(s_time_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_map(s_time_keyboard, LV_KEYBOARD_MODE_USER_1, s_time_kb_map, s_time_kb_ctrl);
    bind_keyboard(s_time_keyboard);

    attach_time_text_field(s_manual_date_ta);
    attach_manual_time_field(s_manual_time_ta);
    time_settings_refresh_fields();

    create_back_button(s_time_screen, time_settings_back_cb);
}

void screen_settings_create(void)
{
    if (s_hub_screen != NULL) {
        return;
    }

    create_hub_screen();
    create_temp_range_screen();
    create_shelly_screen();
    create_safety_screen();
    create_time_screen();
    create_co2_screen();
    create_filter_screen();
    ui_nav_set_settings_screen(s_hub_screen);
}

void screen_settings_show(void)
{
    if (s_hub_screen == NULL) {
        return;
    }
    lv_screen_load(s_hub_screen);
}
