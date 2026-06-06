#include "tile_connections.h"

#include "connection_status.h"
#include "tile_common.h"

#define LABEL_COLOR       0xC9D1D9
#define LED_ON_COLOR      0x3FB950
#define LED_OFF_COLOR     0xF85149
#define LED_SIZE          10

static void set_led_state(lv_obj_t *led, bool on)
{
    lv_obj_set_style_bg_color(led, lv_color_hex(on ? LED_ON_COLOR : LED_OFF_COLOR), 0);
}

static lv_obj_t *create_led(lv_obj_t *parent)
{
    lv_obj_t *led = lv_obj_create(parent);
    lv_obj_remove_style_all(led);
    lv_obj_set_size(led, LED_SIZE, LED_SIZE);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    lv_obj_remove_flag(led, LV_OBJ_FLAG_SCROLLABLE);
    set_led_state(led, false);
    return led;
}

static lv_obj_t *create_status_item(lv_obj_t *parent, connection_id_t id, lv_obj_t **led_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *led = create_led(row);
    *led_out = led;

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, connection_status_label(id));
    lv_obj_set_style_text_color(label, lv_color_hex(LABEL_COLOR), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    return row;
}

tile_connections_t tile_connections_create(lv_obj_t *parent)
{
    tile_connections_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(tile.root, 12, 0);
    lv_obj_set_style_pad_ver(tile.root, 8, 0);
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < CONNECTION_COUNT; i++) {
        (void)create_status_item(tile.root, (connection_id_t)i, &tile.leds[i]);
    }

    tile_connections_update(&tile);
    return tile;
}

void tile_connections_update(tile_connections_t *tile)
{
    if (tile == NULL) {
        return;
    }

    for (int i = 0; i < CONNECTION_COUNT; i++) {
        if (tile->leds[i] != NULL) {
            set_led_state(tile->leds[i], connection_status_is_on((connection_id_t)i));
        }
    }
}
