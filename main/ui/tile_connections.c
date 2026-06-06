#include "tile_connections.h"

#include "connection_status.h"
#include "tile_common.h"

#define TILE_TITLE_COLOR  0x8B949E
#define LABEL_COLOR       0xC9D1D9
#define LED_ON_COLOR      0x3FB950
#define LED_OFF_COLOR     0xF85149
#define LED_SIZE          12

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

static lv_obj_t *create_status_row(lv_obj_t *parent, connection_id_t id, lv_obj_t **led_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
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
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tile.root, 10, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Connections");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *grid = lv_obj_create(tile.root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(grid, 16, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);

    for (int i = 0; i < CONNECTION_COUNT; i++) {
        const int col = i % 2;
        const int row = i / 2;

        lv_obj_t *row_obj = create_status_row(grid, (connection_id_t)i, &tile.leds[i]);
        lv_obj_set_grid_cell(row_obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_CENTER, row, 1);
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
