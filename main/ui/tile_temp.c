#include "tile_temp.h"

#include "temp_source.h"
#include "storage/aquapilot_settings.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR    0x8B949E
#define TILE_VALUE_COLOR    0xE6EDF3
#define TILE_STATUS_COLOR   0x6E7681
#define TILE_DEFAULT_BG       0x161B22
#define TILE_DEFAULT_BORDER   0x30363D
#define TILE_IN_RANGE_BG      0x13261B
#define TILE_IN_RANGE_BORDER  0x3FB950
#define TILE_OUT_RANGE_BG     0x261318
#define TILE_OUT_RANGE_BORDER 0xF85149

typedef enum {
    TEMP_TILE_NEUTRAL = 0,
    TEMP_TILE_IN_RANGE,
    TEMP_TILE_OUT_RANGE,
} temp_tile_range_state_t;

static void apply_panel_colors(lv_obj_t *root, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_radius(root, 12, 0);
}

static void apply_range_style(lv_obj_t *root, temp_tile_range_state_t state)
{
    switch (state) {
    case TEMP_TILE_IN_RANGE:
        apply_panel_colors(root, TILE_IN_RANGE_BG, TILE_IN_RANGE_BORDER);
        break;
    case TEMP_TILE_OUT_RANGE:
        apply_panel_colors(root, TILE_OUT_RANGE_BG, TILE_OUT_RANGE_BORDER);
        break;
    default:
        apply_panel_colors(root, TILE_DEFAULT_BG, TILE_DEFAULT_BORDER);
        break;
    }
}

tile_temp_t tile_temp_create(lv_obj_t *parent)
{
    tile_temp_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 8, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Tank Temperature");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.value_label = lv_label_create(tile.root);
    lv_label_set_text(tile.value_label, "--.- °F");
    lv_obj_set_style_text_color(tile.value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.value_label, &lv_font_montserrat_36, 0);

    tile.status_label = lv_label_create(tile.root);
    lv_label_set_text(tile.status_label, temp_source_status_text());
    lv_obj_set_style_text_color(tile.status_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.status_label, &lv_font_montserrat_16, 0);

    tile_temp_update(&tile);
    return tile;
}

void tile_temp_update(tile_temp_t *tile)
{
    if (tile == NULL || tile->value_label == NULL) {
        return;
    }

    char buf[16];
    if (temp_source_has_reading()) {
        snprintf(buf, sizeof(buf), "%.1f °F", temp_source_get_tank_temp_f());
    } else {
        snprintf(buf, sizeof(buf), "--.- °F");
    }
    lv_label_set_text(tile->value_label, buf);

    if (tile->status_label != NULL) {
        lv_label_set_text(tile->status_label, temp_source_status_text());
    }

    temp_tile_range_state_t state = TEMP_TILE_NEUTRAL;
    if (temp_source_has_reading()) {
        state = aquapilot_settings_temp_contains(temp_source_get_tank_temp_f()) ? TEMP_TILE_IN_RANGE
                                                                             : TEMP_TILE_OUT_RANGE;
    }
    if (tile->root != NULL) {
        apply_range_style(tile->root, state);
    }
}
