#include "tile_co2_status.h"

#include "esp_timer.h"
#include "safety/air_power_monitor.h"
#include "safety/co2_power_monitor.h"
#include "schedule/air_schedule.h"
#include "schedule/co2_schedule.h"
#include "storage/aquapilot_settings.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR        0x8B949E
#define TILE_VALUE_COLOR        0xE6EDF3
#define TILE_VALUE_ON           0x3FB950
#define TILE_STATUS_COLOR       0x6E7681
#define TILE_DEFAULT_BG         0x161B22
#define TILE_DEFAULT_BORDER     0x30363D
#define TILE_ON_BG              0x13261B
#define TILE_ON_BORDER          0x3FB950
#define TILE_ALARM_BG           0x3D0A0A
#define TILE_ALARM_BORDER       0xFF4444
#define TILE_ALARM_TEXT         0xFFCCCC
#define ALARM_FLASH_PERIOD_US   500000ULL

typedef enum {
    GAS_TILE_OFF = 0,
    GAS_TILE_CO2_ON,
    GAS_TILE_AIR_ON,
    GAS_TILE_BOTH_ON,
} gas_tile_state_t;

static void apply_panel_colors(lv_obj_t *root, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_radius(root, 12, 0);
}

static void apply_on_style(lv_obj_t *root)
{
    apply_panel_colors(root, TILE_ON_BG, TILE_ON_BORDER);
}

static void apply_off_style(lv_obj_t *root)
{
    apply_panel_colors(root, TILE_DEFAULT_BG, TILE_DEFAULT_BORDER);
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

static gas_tile_state_t gas_tile_state(void)
{
    const bool co2_on = co2_schedule_is_injection_active();
    const bool air_on = air_schedule_desired_plug_on();

    bool simultaneous = false;
    aquapilot_settings_get_co2_air_simultaneous(&simultaneous);

    if (co2_on && air_on && simultaneous) {
        return GAS_TILE_BOTH_ON;
    }

    if (co2_on) {
        return GAS_TILE_CO2_ON;
    }

    if (air_on) {
        return GAS_TILE_AIR_ON;
    }

    return GAS_TILE_OFF;
}

static bool gas_tile_alarm_active(const char **message_out)
{
    if (co2_power_monitor_alarm_active()) {
        if (message_out != NULL) {
            *message_out = "CO2 on but not\ndrawing power";
        }
        return true;
    }

    if (co2_power_monitor_off_leak_alarm_active()) {
        if (message_out != NULL) {
            *message_out = "CO2 should be off\nbut drawing power";
        }
        return true;
    }

    if (air_power_monitor_alarm_active()) {
        if (message_out != NULL) {
            *message_out = "Air on but not\ndrawing power";
        }
        return true;
    }

    if (air_power_monitor_off_leak_alarm_active()) {
        if (message_out != NULL) {
            *message_out = "Air should be off\nbut drawing power";
        }
        return true;
    }

    return false;
}

static const char *state_label(gas_tile_state_t state)
{
    switch (state) {
    case GAS_TILE_CO2_ON:
        return "CO2 ON";
    case GAS_TILE_AIR_ON:
        return "AIR ON";
    case GAS_TILE_BOTH_ON:
        return "BOTH ON";
    default:
        return "OFF";
    }
}

static bool read_display_watts(gas_tile_state_t state, uint16_t *watts_out)
{
    if (watts_out == NULL) {
        return false;
    }

    if (state == GAS_TILE_CO2_ON || state == GAS_TILE_BOTH_ON) {
        return co2_power_monitor_get_watts(watts_out);
    }

    if (state == GAS_TILE_AIR_ON) {
        return air_power_monitor_get_watts(watts_out);
    }

    return false;
}

tile_co2_status_t tile_co2_status_create(lv_obj_t *parent)
{
    tile_co2_status_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 8, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Air Status");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.value_label = lv_label_create(tile.root);
    lv_label_set_text(tile.value_label, "OFF");
    lv_obj_set_style_text_color(tile.value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.value_label, &lv_font_montserrat_36, 0);

    tile.watts_label = lv_label_create(tile.root);
    lv_label_set_text(tile.watts_label, "-- W");
    lv_obj_set_style_text_color(tile.watts_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.watts_label, &lv_font_montserrat_20, 0);

    tile.status_label = lv_label_create(tile.root);
    lv_label_set_text(tile.status_label, "");
    lv_obj_add_flag(tile.status_label, LV_OBJ_FLAG_HIDDEN);

    tile_co2_status_update(&tile);
    return tile;
}

void tile_co2_status_update(tile_co2_status_t *tile)
{
    if (tile == NULL || tile->value_label == NULL) {
        return;
    }

    const gas_tile_state_t state = gas_tile_state();
    const char *alarm_message = NULL;
    const bool alarm = gas_tile_alarm_active(&alarm_message);

    if (alarm && alarm_message != NULL) {
        tile_show_alarm_message(tile->value_label, alarm_message, TILE_ALARM_TEXT);
        set_label_hidden(tile->watts_label, true);
        set_label_hidden(tile->status_label, true);
    } else {
        const bool active = state != GAS_TILE_OFF;
        tile_restore_value_label(tile->value_label, active ? TILE_VALUE_ON : TILE_VALUE_COLOR);
        lv_label_set_text(tile->value_label, state_label(state));
        set_label_hidden(tile->watts_label, false);

        if (tile->watts_label != NULL) {
            char watts_buf[16];
            uint16_t watts = 0;
            if (read_display_watts(state, &watts)) {
                snprintf(watts_buf, sizeof(watts_buf), "%u W", (unsigned)watts);
            } else {
                snprintf(watts_buf, sizeof(watts_buf), "-- W");
            }
            lv_label_set_text(tile->watts_label, watts_buf);
            lv_obj_set_style_text_color(tile->watts_label, lv_color_hex(TILE_STATUS_COLOR), 0);
        }

        set_label_hidden(tile->status_label, true);
    }

    if (tile->root != NULL) {
        if (alarm) {
            const bool flash_on = (esp_timer_get_time() / ALARM_FLASH_PERIOD_US) % 2 == 0;
            apply_alarm_style(tile->root, flash_on);
        } else if (state != GAS_TILE_OFF) {
            apply_on_style(tile->root);
        } else {
            apply_off_style(tile->root);
        }
    }
}

bool tile_co2_status_needs_fast_update(void)
{
    return gas_tile_alarm_active(NULL);
}
