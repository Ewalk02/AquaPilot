#include "tile_temp_history.h"

#include "storage/temp_history.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_LABEL_COLOR  0x6E7681
#define TANK_COLOR        0xF0883E
#define AMBIENT_COLOR     0x58A6FF
#define CHART_BG          0x0D1117
#define CHART_BORDER      0x30363D
#define CHART_GRID        0x21262D
#define Y_AXIS_WIDTH      34

static int32_t s_tank_values[TEMP_HISTORY_SLOTS];
static int32_t s_ambient_values[TEMP_HISTORY_SLOTS];

static lv_obj_t *create_legend_item(lv_obj_t *parent, uint32_t color_hex, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color_hex), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(TILE_LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    return row;
}

static lv_obj_t *create_axis_label(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(TILE_LABEL_COLOR), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(lbl, Y_AXIS_WIDTH);
    return lbl;
}

static void update_y_axis_labels(tile_temp_history_t *tile, int32_t y_min, int32_t y_max)
{
    if (tile == NULL) {
        return;
    }

    char buf[16];
    const int32_t y_mid = (y_min + y_max) / 2;

    if (tile->y_max_label != NULL) {
        snprintf(buf, sizeof(buf), "%ld", (long)y_max);
        lv_label_set_text(tile->y_max_label, buf);
    }
    if (tile->y_mid_label != NULL) {
        snprintf(buf, sizeof(buf), "%ld", (long)y_mid);
        lv_label_set_text(tile->y_mid_label, buf);
    }
    if (tile->y_min_label != NULL) {
        snprintf(buf, sizeof(buf), "%ld", (long)y_min);
        lv_label_set_text(tile->y_min_label, buf);
    }
}

tile_temp_history_t tile_temp_history_create(lv_obj_t *parent)
{
    tile_temp_history_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tile.root, 8, 0);
    lv_obj_set_style_pad_row(tile.root, 4, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *legend = lv_obj_create(tile.root);
    lv_obj_remove_style_all(legend);
    lv_obj_set_width(legend, LV_PCT(100));
    lv_obj_set_height(legend, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(legend, 12, 0);
    lv_obj_remove_flag(legend, LV_OBJ_FLAG_SCROLLABLE);
    create_legend_item(legend, TANK_COLOR, "Tank");
    create_legend_item(legend, AMBIENT_COLOR, "Ambient");

    lv_obj_t *chart_row = lv_obj_create(tile.root);
    lv_obj_remove_style_all(chart_row);
    lv_obj_set_width(chart_row, LV_PCT(100));
    lv_obj_set_flex_grow(chart_row, 1);
    lv_obj_set_flex_flow(chart_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chart_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(chart_row, 4, 0);
    lv_obj_remove_flag(chart_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *y_axis = lv_obj_create(chart_row);
    lv_obj_remove_style_all(y_axis);
    lv_obj_set_width(y_axis, Y_AXIS_WIDTH);
    lv_obj_set_height(y_axis, LV_PCT(100));
    lv_obj_set_flex_flow(y_axis, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(y_axis, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(y_axis, LV_OBJ_FLAG_SCROLLABLE);

    tile.y_max_label = create_axis_label(y_axis);
    tile.y_mid_label = create_axis_label(y_axis);
    tile.y_min_label = create_axis_label(y_axis);

    tile.chart = lv_chart_create(chart_row);
    lv_obj_set_width(tile.chart, LV_PCT(100));
    lv_obj_set_flex_grow(tile.chart, 1);
    lv_obj_set_height(tile.chart, LV_PCT(100));
    lv_obj_set_style_bg_color(tile.chart, lv_color_hex(CHART_BG), 0);
    lv_obj_set_style_bg_opa(tile.chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tile.chart, lv_color_hex(CHART_BORDER), 0);
    lv_obj_set_style_border_width(tile.chart, 1, 0);
    lv_obj_set_style_radius(tile.chart, 6, 0);
    lv_obj_set_style_line_color(tile.chart, lv_color_hex(CHART_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_opa(tile.chart, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile.chart, 2, 0);
    lv_chart_set_type(tile.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(tile.chart, TEMP_HISTORY_SLOTS);
    lv_chart_set_update_mode(tile.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(tile.chart, 4, 6);

    tile.tank_series = lv_chart_add_series(tile.chart, lv_color_hex(TANK_COLOR), LV_CHART_AXIS_PRIMARY_Y);
    tile.ambient_series = lv_chart_add_series(tile.chart, lv_color_hex(AMBIENT_COLOR), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *axis_row = lv_obj_create(tile.root);
    lv_obj_remove_style_all(axis_row);
    lv_obj_set_width(axis_row, LV_PCT(100));
    lv_obj_set_height(axis_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(axis_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(axis_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(axis_row, Y_AXIS_WIDTH + 4, 0);
    lv_obj_remove_flag(axis_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left_lbl = lv_label_create(axis_row);
    lv_label_set_text(left_lbl, "-48h");
    lv_obj_set_style_text_color(left_lbl, lv_color_hex(TILE_LABEL_COLOR), 0);
    lv_obj_set_style_text_font(left_lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *mid_lbl = lv_label_create(axis_row);
    lv_label_set_text(mid_lbl, "-24h");
    lv_obj_set_style_text_color(mid_lbl, lv_color_hex(TILE_LABEL_COLOR), 0);
    lv_obj_set_style_text_font(mid_lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *right_lbl = lv_label_create(axis_row);
    lv_label_set_text(right_lbl, "Now");
    lv_obj_set_style_text_color(right_lbl, lv_color_hex(TILE_LABEL_COLOR), 0);
    lv_obj_set_style_text_font(right_lbl, &lv_font_montserrat_12, 0);

    tile_temp_history_update(&tile);
    return tile;
}

void tile_temp_history_update(tile_temp_history_t *tile)
{
    if (tile == NULL || tile->chart == NULL || tile->tank_series == NULL || tile->ambient_series == NULL) {
        return;
    }

    int32_t y_min = 70;
    int32_t y_max = 80;
    const bool has_data =
        temp_history_get_chart_values(s_tank_values, s_ambient_values, TEMP_HISTORY_SLOTS, &y_min, &y_max);

    for (uint16_t i = 0; i < TEMP_HISTORY_SLOTS; i++) {
        int32_t tank = s_tank_values[i];
        int32_t ambient = s_ambient_values[i];
        if (tank == INT32_MAX) {
            tank = LV_CHART_POINT_NONE;
        }
        if (ambient == INT32_MAX) {
            ambient = LV_CHART_POINT_NONE;
        }
        lv_chart_set_series_value_by_id(tile->chart, tile->tank_series, i, tank);
        lv_chart_set_series_value_by_id(tile->chart, tile->ambient_series, i, ambient);
    }

    lv_chart_set_axis_range(tile->chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    update_y_axis_labels(tile, y_min, y_max);
    lv_chart_refresh(tile->chart);

    (void)has_data;
}
