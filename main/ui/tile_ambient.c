#include "tile_ambient.h"

#include "sensors/sht3x_sensor.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR  0x8B949E
#define TILE_VALUE_COLOR  0xE6EDF3

tile_ambient_t tile_ambient_create(lv_obj_t *parent)
{
    tile_ambient_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(tile.root, 6, 0);
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile.root, 6, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Ambient");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    tile.value_label = lv_label_create(tile.root);
    lv_label_set_text(tile.value_label, "--.-");
    lv_obj_set_style_text_color(tile.value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.value_label, &lv_font_montserrat_20, 0);

    tile_ambient_update(&tile);
    return tile;
}

void tile_ambient_update(tile_ambient_t *tile)
{
    if (tile == NULL || tile->value_label == NULL) {
        return;
    }

    char buf[16];
    if (sht3x_sensor_has_reading()) {
        snprintf(buf, sizeof(buf), "%.1f F", sht3x_sensor_get_temp_f());
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }

    lv_label_set_text(tile->value_label, buf);
}
