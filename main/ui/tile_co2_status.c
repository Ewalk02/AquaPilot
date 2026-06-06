#include "tile_co2_status.h"

#include "esp_timer.h"
#include "safety/co2_power_monitor.h"
#include "schedule/co2_schedule.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR      0x8B949E
#define TILE_VALUE_COLOR      0xE6EDF3
#define TILE_STATUS_COLOR     0x6E7681
#define TILE_DEFAULT_BG       0x161B22
#define TILE_DEFAULT_BORDER   0x30363D
#define TILE_ON_BG            0x13261B
#define TILE_ON_BORDER        0x3FB950
#define TILE_ALARM_BG         0x3D0A0A
#define TILE_ALARM_BORDER     0xFF4444
#define TILE_ALARM_TEXT       0xFFCCCC
#define ALARM_FLASH_PERIOD_US 500000ULL

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
    lv_label_set_text(title, "CO2 Status");
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

    const bool active = co2_schedule_is_injection_active();
    const bool alarm = co2_power_monitor_alarm_active();

    if (alarm) {
        tile_show_alarm_message(tile->value_label, "CO2 on but not\ndrawing power", TILE_ALARM_TEXT);
        set_label_hidden(tile->watts_label, true);
        set_label_hidden(tile->status_label, true);
    } else {
        tile_restore_value_label(tile->value_label, TILE_VALUE_COLOR);
        lv_label_set_text(tile->value_label, active ? "ON" : "OFF");
        set_label_hidden(tile->watts_label, false);

        if (tile->watts_label != NULL) {
            char watts_buf[16];
            uint16_t watts = 0;
            if (co2_power_monitor_get_watts(&watts)) {
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
        } else if (active) {
            apply_on_style(tile->root);
        } else {
            apply_off_style(tile->root);
        }
    }
}

bool tile_co2_status_needs_fast_update(void)
{
    return co2_power_monitor_alarm_active();
}
