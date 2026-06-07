#include "tile_clock.h"

#include "schedule/aquapilot_time.h"
#include "tile_common.h"

#include <stdio.h>
#include <time.h>

#define TILE_VALUE_COLOR  0xE6EDF3

tile_clock_t tile_clock_create(lv_obj_t *parent)
{
    tile_clock_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(tile.root, 6, 0);
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    tile.value_label = lv_label_create(tile.root);
    lv_label_set_text(tile.value_label, "--:--");
    lv_obj_set_style_text_color(tile.value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(tile.value_label, LV_TEXT_ALIGN_CENTER, 0);

    tile_clock_update(&tile);
    return tile;
}

void tile_clock_update(tile_clock_t *tile)
{
    if (tile == NULL || tile->value_label == NULL) {
        return;
    }

    char buf[8];
    if (aquapilot_time_is_ready()) {
        const time_t now = time(NULL);
        struct tm local = {0};
        if (localtime_r(&now, &local) != NULL) {
            snprintf(buf, sizeof(buf), "%02d:%02u", local.tm_hour, (unsigned)local.tm_min);
        } else {
            snprintf(buf, sizeof(buf), "--:--");
        }
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }

    lv_label_set_text(tile->value_label, buf);
}
