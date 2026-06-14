#include "tile_feeder_status.h"

#include <stdio.h>

#include "feeder/feeder_client.h"
#include "schedule/feeder_service.h"
#include "tile_common.h"

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

static void apply_panel_colors(lv_obj_t *root, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_radius(root, 12, 0);
}

static void apply_default_style(lv_obj_t *root)
{
    apply_panel_colors(root, TILE_DEFAULT_BG, TILE_DEFAULT_BORDER);
}

static void apply_ok_style(lv_obj_t *root)
{
    apply_panel_colors(root, TILE_ON_BG, TILE_ON_BORDER);
}

static void apply_alarm_style(lv_obj_t *root)
{
    apply_panel_colors(root, TILE_ALARM_BG, TILE_ALARM_BORDER);
}

static void set_value_font(lv_obj_t *label, bool large)
{
    if (label == NULL) {
        return;
    }
    lv_obj_set_style_text_font(label, large ? &lv_font_montserrat_36 : &lv_font_montserrat_20, 0);
}

tile_feeder_status_t tile_feeder_status_create(lv_obj_t *parent)
{
    tile_feeder_status_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 8, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Automatic Feeder");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.schedule_label = lv_label_create(tile.root);
    lv_label_set_text(tile.schedule_label, "OFF");
    lv_obj_set_style_text_color(tile.schedule_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.schedule_label, &lv_font_montserrat_36, 0);
    lv_obj_set_width(tile.schedule_label, LV_PCT(100));
    lv_obj_set_style_text_align(tile.schedule_label, LV_TEXT_ALIGN_CENTER, 0);

    tile.countdown_label = lv_label_create(tile.root);
    lv_label_set_text(tile.countdown_label, "");
    lv_obj_set_style_text_color(tile.countdown_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.countdown_label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(tile.countdown_label, LV_PCT(100));
    lv_obj_set_style_text_align(tile.countdown_label, LV_TEXT_ALIGN_CENTER, 0);

    tile_feeder_status_update(&tile);
    return tile;
}

void tile_feeder_status_update(tile_feeder_status_t *tile)
{
    if (tile == NULL) {
        return;
    }

    char schedule_line[32];
    char countdown[48];
    countdown[0] = '\0';

    const bool feeding = feeder_service_is_feeding();
    const bool missed = feeder_service_is_feed_missed();

    if (feeding) {
        apply_default_style(tile->root);
        tile_restore_value_label(tile->schedule_label, TILE_VALUE_COLOR);
        snprintf(schedule_line, sizeof(schedule_line), "Feeding Fish");
        set_value_font(tile->schedule_label, false);
        lv_label_set_text(tile->countdown_label, "");
    } else if (feeder_service_show_feed_complete()) {
        apply_default_style(tile->root);
        tile_restore_value_label(tile->schedule_label, TILE_VALUE_COLOR);
        snprintf(schedule_line, sizeof(schedule_line), "Feeding Complete");
        set_value_font(tile->schedule_label, false);
        lv_label_set_text(tile->countdown_label, "");
    } else if (missed) {
        apply_alarm_style(tile->root);
        tile_show_alarm_message(tile->schedule_label, "Feeding Missed", TILE_ALARM_TEXT);
        lv_label_set_text(tile->countdown_label, "");
    } else {
        const bool enabled = feeder_service_is_enabled();
        const bool healthy = enabled && feeder_client_is_online() && !missed;

        if (healthy) {
            apply_ok_style(tile->root);
        } else {
            apply_default_style(tile->root);
        }

        if (!enabled) {
            tile_restore_value_label(tile->schedule_label, TILE_VALUE_COLOR);
            snprintf(schedule_line, sizeof(schedule_line), "OFF");
            set_value_font(tile->schedule_label, true);
        } else {
            tile_restore_value_label(tile->schedule_label, healthy ? TILE_VALUE_ON : TILE_VALUE_COLOR);
            uint8_t times = 0;
            if (feeder_service_get_times_per_day(&times)) {
                snprintf(schedule_line, sizeof(schedule_line), "%u x DAILY", (unsigned)times);
            } else {
                snprintf(schedule_line, sizeof(schedule_line), "--");
            }
            set_value_font(tile->schedule_label, true);
            feeder_service_format_countdown(countdown, sizeof(countdown));
        }
    }

    if (!missed && tile->schedule_label != NULL) {
        lv_label_set_text(tile->schedule_label, schedule_line);
    }
    if (!missed && tile->countdown_label != NULL) {
        lv_label_set_text(tile->countdown_label, countdown);
    }
}
