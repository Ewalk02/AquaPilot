#include "screen_wifi.h"

#include "lvgl.h"
#include "net/wifi_manager.h"
#include "storage/wifi_creds_nvs.h"
#include "screen_settings.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_BG_COLOR   0x0D1117
#define PANEL_BG_COLOR    0x161B22
#define BORDER_COLOR      0x30363D
#define TITLE_COLOR       0xE6EDF3
#define LABEL_COLOR       0x8B949E
#define STATUS_COLOR      0x6E7681

static lv_obj_t *s_screen;
static lv_obj_t *s_ta_ssid;
static lv_obj_t *s_ta_password;
static lv_obj_t *s_label_status;
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

static void hide_keyboard(void)
{
    if (s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
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

static void bind_keyboard(lv_obj_t *kb)
{
    lv_obj_add_event_cb(kb, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, ta);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_defocus_cb(lv_event_t *e)
{
    (void)e;
    hide_keyboard();
}

static void btn_back_cb(lv_event_t *e)
{
    (void)e;
    hide_keyboard();
    screen_settings_show();
}

static void btn_connect_cb(lv_event_t *e)
{
    (void)e;

    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_password);

    if (ssid == NULL || ssid[0] == '\0') {
        if (s_label_status != NULL) {
            lv_label_set_text(s_label_status, "SSID is required");
        }
        return;
    }

    hide_keyboard();
    aquapilot_wifi_apply_credentials_async(ssid, pass != NULL ? pass : "");

    if (s_label_status != NULL) {
        lv_label_set_text(s_label_status, "Saving credentials, connecting…");
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    screen_wifi_refresh_status();
}

void screen_wifi_refresh_status(void)
{
    if (s_label_status != NULL) {
        lv_label_set_text(s_label_status, aquapilot_wifi_status_text());
    }
}

void screen_wifi_create(void)
{
    if (s_screen != NULL) {
        return;
    }

    s_screen = lv_obj_create(NULL);
    apply_screen_style(s_screen);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_screen, 12, 0);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Wi-Fi Configuration");
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    s_label_status = lv_label_create(s_screen);
    lv_label_set_text(s_label_status, aquapilot_wifi_status_text());
    lv_obj_set_style_text_color(s_label_status, lv_color_hex(STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_label_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_label_status, LV_PCT(100));

    lv_obj_t *form = lv_obj_create(s_screen);
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
    lv_obj_set_style_pad_row(form, 8, 0);

    lv_obj_t *lbl_ssid = lv_label_create(form);
    lv_label_set_text(lbl_ssid, "Network name (SSID)");
    lv_obj_set_style_text_color(lbl_ssid, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl_ssid, &lv_font_montserrat_16, 0);

    s_ta_ssid = lv_textarea_create(form);
    lv_obj_set_width(s_ta_ssid, LV_PCT(100));
    lv_obj_set_height(s_ta_ssid, 48);
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_textarea_set_max_length(s_ta_ssid, AQUAPILOT_WIFI_SSID_MAX);
    lv_textarea_set_placeholder_text(s_ta_ssid, "SSID");
    lv_obj_add_event_cb(s_ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_ssid, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *lbl_pass = lv_label_create(form);
    lv_label_set_text(lbl_pass, "Password");
    lv_obj_set_style_text_color(lbl_pass, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl_pass, &lv_font_montserrat_16, 0);

    s_ta_password = lv_textarea_create(form);
    lv_obj_set_width(s_ta_password, LV_PCT(100));
    lv_obj_set_height(s_ta_password, 48);
    lv_textarea_set_one_line(s_ta_password, true);
    lv_textarea_set_password_mode(s_ta_password, true);
    lv_textarea_set_max_length(s_ta_password, AQUAPILOT_WIFI_PASS_MAX);
    lv_textarea_set_placeholder_text(s_ta_password, "Password (leave empty if open)");
    lv_obj_add_event_cb(s_ta_password, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_password, ta_defocus_cb, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *btn_row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 16, 0);

    lv_obj_t *btn_connect = lv_button_create(btn_row);
    lv_obj_set_size(btn_connect, 220, 48);
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connect");
    lv_obj_center(lbl_connect);

    lv_obj_t *btn_back = lv_button_create(btn_row);
    lv_obj_set_size(btn_back, 140, 48);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Back");
    lv_obj_center(lbl_back);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_set_width(s_keyboard, LV_PCT(100));
    lv_obj_set_height(s_keyboard, 180);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    bind_keyboard(s_keyboard);

    char ssid[AQUAPILOT_WIFI_SSID_MAX + 1];
    char password[AQUAPILOT_WIFI_PASS_MAX + 1];
    aquapilot_wifi_get_credentials(ssid, sizeof(ssid), password, sizeof(password));
    if (ssid[0] != '\0') {
        lv_textarea_set_text(s_ta_ssid, ssid);
    }
    if (password[0] != '\0') {
        lv_textarea_set_text(s_ta_password, password);
    }

    s_status_timer = lv_timer_create(status_timer_cb, 2000, NULL);
}

void screen_wifi_show(void)
{
    if (s_screen == NULL) {
        return;
    }

    char ssid[AQUAPILOT_WIFI_SSID_MAX + 1];
    char password[AQUAPILOT_WIFI_PASS_MAX + 1];
    aquapilot_wifi_get_credentials(ssid, sizeof(ssid), password, sizeof(password));
    if (s_ta_ssid != NULL && ssid[0] != '\0') {
        lv_textarea_set_text(s_ta_ssid, ssid);
    }
    if (s_ta_password != NULL) {
        lv_textarea_set_text(s_ta_password, password);
    }

    screen_wifi_refresh_status();
    hide_keyboard();
    lv_screen_load(s_screen);
}
