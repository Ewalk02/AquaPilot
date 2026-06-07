#include "tile_feeder_status.h"

#include <stdio.h>

#include "schedule/feeder_service.h"
#include "tile_common.h"

#define TILE_TITLE_COLOR   0x8B949E
#define TILE_VALUE_COLOR   0xE6EDF3
#define TILE_STATUS_COLOR  0x6E7681

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

    tile.countdown_label = lv_label_create(tile.root);
    lv_label_set_text(tile.countdown_label, "");
    lv_obj_set_style_text_color(tile.countdown_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.countdown_label, &lv_font_montserrat_20, 0);

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

    if (feeder_service_is_feeding()) {
        snprintf(schedule_line, sizeof(schedule_line), "FEEDING");
        countdown[0] = '\0';
    } else if (!feeder_service_is_enabled()) {
        snprintf(schedule_line, sizeof(schedule_line), "OFF");
        countdown[0] = '\0';
    } else {
        uint8_t times = 0;
        if (feeder_service_get_times_per_day(&times)) {
            snprintf(schedule_line, sizeof(schedule_line), "%u× DAILY", (unsigned)times);
        } else {
            snprintf(schedule_line, sizeof(schedule_line), "--");
        }
        feeder_service_format_countdown(countdown, sizeof(countdown));
    }

    if (tile->schedule_label != NULL) {
        lv_label_set_text(tile->schedule_label, schedule_line);
    }
    if (tile->countdown_label != NULL) {
        lv_label_set_text(tile->countdown_label, countdown);
    }
}
