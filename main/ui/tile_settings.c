#include "tile_settings.h"

#include "screen_settings.h"
#include "tile_common.h"

#define TILE_VALUE_COLOR  0xE6EDF3

static void settings_click_cb(lv_event_t *e)
{
    (void)e;
    screen_settings_show();
}

tile_settings_t tile_settings_create(lv_obj_t *parent)
{
    tile_settings_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(tile.root, 8, 0);
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(tile.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tile.root, settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    return tile;
}
