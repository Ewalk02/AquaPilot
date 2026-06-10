#include "tile_light_status.h"

#include "light/light_service.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR      0x8B949E
#define TILE_VALUE_COLOR      0xE6EDF3
#define TILE_VALUE_ON         0x3FB950
#define TILE_STATUS_COLOR     0x6E7681
#define TILE_DEFAULT_BG       0x161B22
#define TILE_DEFAULT_BORDER   0x30363D
#define TILE_ON_BG            0x13261B
#define TILE_ON_BORDER        0x3FB950

static void apply_panel_colors(lv_obj_t *root, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_radius(root, 12, 0);
}

tile_light_status_t tile_light_status_create(lv_obj_t *parent)
{
    tile_light_status_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 8, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Light Status");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.value_label = lv_label_create(tile.root);
    lv_label_set_text(tile.value_label, "--");
    lv_obj_set_style_text_color(tile.value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile.value_label, &lv_font_montserrat_36, 0);

    tile.brightness_label = lv_label_create(tile.root);
    lv_label_set_text(tile.brightness_label, "--");
    lv_obj_set_style_text_color(tile.brightness_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(tile.brightness_label, &lv_font_montserrat_20, 0);

    tile_light_status_update(&tile);
    return tile;
}

void tile_light_status_update(tile_light_status_t *tile)
{
    if (tile == NULL || tile->value_label == NULL) {
        return;
    }

    const bool known = light_service_status_is_known();
    const bool on = light_service_is_on();
    const light_status_mode_t mode = light_service_get_mode();

    if (!known) {
        tile_restore_value_label(tile->value_label, TILE_VALUE_COLOR);
        lv_label_set_text(tile->value_label, "--");
    } else {
        tile_restore_value_label(tile->value_label, on ? TILE_VALUE_ON : TILE_VALUE_COLOR);
        lv_label_set_text(tile->value_label, on ? "ON" : "OFF");
    }

    if (tile->brightness_label != NULL) {
        char buf[16];
        if (!known) {
            snprintf(buf, sizeof(buf), "--");
        } else if (mode == LIGHT_STATUS_AUTO) {
            snprintf(buf, sizeof(buf), "Auto");
        } else if (mode == LIGHT_STATUS_MANUAL) {
            snprintf(buf, sizeof(buf), "%u %%", (unsigned)light_service_get_brightness_pct());
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(tile->brightness_label, buf);
        lv_obj_set_style_text_color(tile->brightness_label, lv_color_hex(TILE_STATUS_COLOR), 0);
    }

    if (tile->root != NULL) {
        if (known && on) {
            apply_panel_colors(tile->root, TILE_ON_BG, TILE_ON_BORDER);
        } else {
            apply_panel_colors(tile->root, TILE_DEFAULT_BG, TILE_DEFAULT_BORDER);
        }
    }
}
