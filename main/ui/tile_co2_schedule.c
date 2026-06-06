#include "tile_co2_schedule.h"

#include "schedule/co2_schedule.h"
#include "tile_common.h"

#define TILE_TITLE_COLOR   0x8B949E
#define TILE_VALUE_COLOR   0xC9D1D9
#define TILE_STATUS_COLOR  0x6E7681

tile_co2_schedule_t tile_co2_schedule_create(lv_obj_t *parent)
{
    tile_co2_schedule_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 8, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "CO2 Schedule");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.schedule_label = lv_label_create(tile.root);
    lv_label_set_text(tile.schedule_label, "--:-- - --:--");
    lv_obj_set_style_text_color(tile.schedule_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.schedule_label, &lv_font_montserrat_24, 0);

    tile.countdown_label = lv_label_create(tile.root);
    lv_label_set_text(tile.countdown_label, "");
    lv_obj_set_style_text_color(tile.countdown_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.countdown_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(tile.countdown_label, LV_PCT(100));
    lv_obj_set_style_text_align(tile.countdown_label, LV_TEXT_ALIGN_CENTER, 0);

    tile_co2_schedule_update(&tile);
    return tile;
}

void tile_co2_schedule_update(tile_co2_schedule_t *tile)
{
    if (tile == NULL) {
        return;
    }

    char range[32];
    char countdown[48];

    co2_schedule_format_range(range, sizeof(range));
    co2_schedule_format_countdown(countdown, sizeof(countdown));

    if (tile->schedule_label != NULL) {
        lv_label_set_text(tile->schedule_label, range);
    }
    if (tile->countdown_label != NULL) {
        lv_label_set_text(tile->countdown_label, countdown);
    }
}
