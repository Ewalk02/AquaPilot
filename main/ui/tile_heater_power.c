#include "tile_heater_power.h"

#include "esp_timer.h"
#include "heater/heater_service.h"
#include "safety/heater_override.h"
#include "storage/aquapilot_settings.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR        0x8B949E
#define TILE_VALUE_COLOR        0xE6EDF3
#define TILE_STATUS_COLOR       0x6E7681
#define TILE_DEFAULT_BG         0x161B22
#define TILE_DEFAULT_BORDER     0x30363D
#define TILE_HEATING_BG         0x261A0D
#define TILE_HEATING_BORDER     0xF0883E
#define TILE_ALARM_BG           0x3D0A0A
#define TILE_ALARM_BORDER       0xFF4444
#define TILE_ALARM_TEXT         0xFFCCCC
#define HEATING_WATTS_THRESHOLD 5
#define ALARM_FLASH_PERIOD_US   500000ULL

static void apply_panel_colors(lv_obj_t *root, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_radius(root, 12, 0);
}

static void update_setpoint_label(lv_obj_t *label)
{
    if (label == NULL) {
        return;
    }

    float setpoint_f = 0.0f;
    char buf[32];
    if (aquapilot_settings_get_heater_setpoint(&setpoint_f)) {
        snprintf(buf, sizeof(buf), "Setpoint: %.1f F", setpoint_f);
    } else {
        snprintf(buf, sizeof(buf), "Setpoint: -- F");
    }
    lv_label_set_text(label, buf);
}

static void apply_power_style(lv_obj_t *root, bool heating)
{
    if (heating) {
        apply_panel_colors(root, TILE_HEATING_BG, TILE_HEATING_BORDER);
    } else {
        apply_panel_colors(root, TILE_DEFAULT_BG, TILE_DEFAULT_BORDER);
    }
}

static void apply_alarm_style(lv_obj_t *root, bool flash_on)
{
    if (flash_on) {
        apply_panel_colors(root, TILE_ALARM_BG, TILE_ALARM_BORDER);
    } else {
        apply_panel_colors(root, TILE_DEFAULT_BG, TILE_ALARM_BORDER);
    }
}

static void set_label_hidden(lv_obj_t *label, bool hidden)
{
    if (label == NULL) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

tile_heater_power_t tile_heater_power_create(lv_obj_t *parent)
{
    tile_heater_power_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 8, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Heater Power Monitor");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.value_label = lv_label_create(tile.root);
    lv_label_set_text(tile.value_label, "-- W");
    lv_obj_set_style_text_color(tile.value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.value_label, &lv_font_montserrat_36, 0);

    tile.status_label = lv_label_create(tile.root);
    update_setpoint_label(tile.status_label);
    lv_obj_set_style_text_color(tile.status_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.status_label, &lv_font_montserrat_16, 0);

    tile_heater_power_update(&tile);
    return tile;
}

void tile_heater_power_update(tile_heater_power_t *tile)
{
    if (tile == NULL || tile->value_label == NULL) {
        return;
    }

    uint16_t watts = 0;
    const bool has_reading = heater_service_get_power_watts(&watts);
    const bool alarm = heater_override_alarm_active();

    if (alarm) {
        const char *message = "Temp High but\nHeater On";
        if (heater_override_alarm_reason() == HEATER_ALARM_PLUG_ON) {
            message = "Heater off but\nplug on power";
        }
        tile_show_alarm_message(tile->value_label, message, TILE_ALARM_TEXT);
        set_label_hidden(tile->status_label, true);
    } else {
        tile_restore_value_label(tile->value_label, TILE_VALUE_COLOR);

        char buf[16];
        if (has_reading) {
            snprintf(buf, sizeof(buf), "%u W", (unsigned)watts);
        } else {
            snprintf(buf, sizeof(buf), "-- W");
        }
        lv_label_set_text(tile->value_label, buf);

        set_label_hidden(tile->status_label, false);
        if (tile->status_label != NULL) {
            update_setpoint_label(tile->status_label);
            lv_obj_set_style_text_color(tile->status_label, lv_color_hex(TILE_STATUS_COLOR), 0);
        }
    }

    if (tile->root != NULL) {
        if (alarm) {
            const bool flash_on = (esp_timer_get_time() / ALARM_FLASH_PERIOD_US) % 2 == 0;
            apply_alarm_style(tile->root, flash_on);
        } else {
            apply_power_style(tile->root, has_reading && watts > HEATING_WATTS_THRESHOLD);
        }
    }
}

bool tile_heater_power_needs_fast_update(void)
{
    return heater_override_alarm_active();
}
