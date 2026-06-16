#include "tile_maintenance.h"

#include "maintenance/maintenance_tracker.h"
#include "tile_common.h"

#include <stdio.h>

#define TILE_TITLE_COLOR    0x8B949E
#define TILE_HEADER_COLOR   0x6E7681
#define TILE_ROW_COLOR      0xC9D1D9
#define TILE_DUE_COLOR      0x8B949E
#define TILE_GREEN_BG       0x13261B
#define TILE_GREEN_BORDER   0x3FB950
#define TILE_YELLOW_BG      0x2A2200
#define TILE_YELLOW_BORDER  0xD29922
#define TILE_RED_BG         0x3D0A0A
#define TILE_RED_BORDER     0xFF4444
#define TILE_NEUTRAL_BG     0x161B22
#define TILE_NEUTRAL_BORDER 0x30363D
#define TILE_DUE_COL_W      72

static void apply_panel_colors(lv_obj_t *root, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_radius(root, 12, 0);
}

static void apply_severity_colors(tile_maintenance_t *tile, maintenance_tile_severity_t severity)
{
    if (tile == NULL || tile->root == NULL) {
        return;
    }

    switch (severity) {
    case MAINT_TILE_SEVERITY_RED:
        apply_panel_colors(tile->root, TILE_RED_BG, TILE_RED_BORDER);
        break;
    case MAINT_TILE_SEVERITY_YELLOW:
        apply_panel_colors(tile->root, TILE_YELLOW_BG, TILE_YELLOW_BORDER);
        break;
    case MAINT_TILE_SEVERITY_GREEN:
        apply_panel_colors(tile->root, TILE_GREEN_BG, TILE_GREEN_BORDER);
        break;
    default:
        apply_panel_colors(tile->root, TILE_NEUTRAL_BG, TILE_NEUTRAL_BORDER);
        break;
    }
}

static lv_obj_t *create_table_label(lv_obj_t *parent, const char *text, uint32_t color, const lv_font_t *font,
                                    lv_text_align_t align, lv_coord_t width)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_align(lbl, align, 0);
    if (width > 0) {
        lv_obj_set_width(lbl, width);
    } else {
        lv_obj_set_flex_grow(lbl, 1);
    }
    if (align == LV_TEXT_ALIGN_LEFT) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }
    return lbl;
}

static lv_obj_t *create_table_row(lv_obj_t *parent, bool header)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, header ? 18 : 24);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    if (!header) {
        lv_obj_set_style_border_color(row, lv_color_hex(TILE_NEUTRAL_BORDER), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_50, 0);
    }
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

tile_maintenance_t tile_maintenance_create(lv_obj_t *parent)
{
    tile_maintenance_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tile.root, 10, 0);
    lv_obj_set_style_pad_row(tile.root, 6, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Maintenance");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *table = lv_obj_create(tile.root);
    lv_obj_remove_style_all(table);
    lv_obj_set_width(table, LV_PCT(100));
    lv_obj_set_flex_grow(table, 1);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(table, 2, 0);
    lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_row = create_table_row(table, true);
    create_table_label(header_row, "Task", TILE_HEADER_COLOR, &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT, 0);
    create_table_label(header_row, "Due", TILE_HEADER_COLOR, &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT,
                       TILE_DUE_COL_W);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = create_table_row(table, false);
        tile.name_labels[i] = create_table_label(row, "--", TILE_ROW_COLOR, &lv_font_montserrat_14,
                                                 LV_TEXT_ALIGN_LEFT, 0);
        tile.due_labels[i] = create_table_label(row, "--", TILE_DUE_COLOR, &lv_font_montserrat_14,
                                              LV_TEXT_ALIGN_RIGHT, TILE_DUE_COL_W);
    }

    tile_maintenance_update(&tile);
    return tile;
}

void tile_maintenance_update(tile_maintenance_t *tile)
{
    if (tile == NULL) {
        return;
    }

    maintenance_activity_t ids[3];
    int days[3];
    maintenance_get_top3(ids, days);

    const maintenance_tile_severity_t severity = maintenance_tile_severity();
    apply_severity_colors(tile, severity);

    for (int i = 0; i < 3; i++) {
        if (tile->name_labels[i] == NULL || tile->due_labels[i] == NULL) {
            continue;
        }

        char due[24];
        maintenance_format_due_text(ids[i], due, sizeof(due));

        lv_label_set_text(tile->name_labels[i], maintenance_activity_label(ids[i]));
        lv_label_set_text(tile->due_labels[i], due);

        uint32_t due_color = TILE_DUE_COLOR;
        if (days[i] < 0) {
            due_color = 0xFF7B72;
        } else if (days[i] <= 2) {
            due_color = 0xD29922;
        }
        lv_obj_set_style_text_color(tile->due_labels[i], lv_color_hex(due_color), 0);
    }
}
