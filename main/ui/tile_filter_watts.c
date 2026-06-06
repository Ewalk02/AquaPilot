#include "tile_filter_watts.h"

#include "safety/filter_power_monitor.h"
#include "storage/aquapilot_settings.h"
#include "tile_common.h"

#include <math.h>
#include <stdio.h>

#define TILE_TITLE_COLOR 0x8B949E
#define TILE_VALUE_COLOR 0xE6EDF3
#define TILE_HINT_COLOR  0x6E7681
#define GAUGE_GREEN      0x238636
#define GAUGE_YELLOW     0xD29922
#define GAUGE_RED        0xDA3633

typedef struct {
    lv_style_t items;
    lv_style_t indicator;
    lv_style_t main;
} gauge_section_styles_t;

static gauge_section_styles_t s_red_style;
static gauge_section_styles_t s_yellow_style;
static gauge_section_styles_t s_green_style;
static bool s_section_styles_ready;

static float s_built_baseline;
static uint8_t s_built_green_pct;
static uint8_t s_built_yellow_pct;
static uint8_t s_built_red_pct;
static int32_t s_built_scale_max;

static void init_section_styles(void)
{
    if (s_section_styles_ready) {
        return;
    }

    lv_style_init(&s_red_style.items);
    lv_style_set_line_width(&s_red_style.items, 0);

    lv_style_init(&s_red_style.indicator);
    lv_style_set_line_width(&s_red_style.indicator, 0);

    lv_style_init(&s_red_style.main);
    lv_style_set_arc_color(&s_red_style.main, lv_color_hex(GAUGE_RED));
    lv_style_set_arc_width(&s_red_style.main, 18);

    lv_style_init(&s_yellow_style.items);
    lv_style_set_line_width(&s_yellow_style.items, 0);

    lv_style_init(&s_yellow_style.indicator);
    lv_style_set_line_width(&s_yellow_style.indicator, 0);

    lv_style_init(&s_yellow_style.main);
    lv_style_set_arc_color(&s_yellow_style.main, lv_color_hex(GAUGE_YELLOW));
    lv_style_set_arc_width(&s_yellow_style.main, 18);

    lv_style_init(&s_green_style.items);
    lv_style_set_line_width(&s_green_style.items, 0);

    lv_style_init(&s_green_style.indicator);
    lv_style_set_line_width(&s_green_style.indicator, 0);

    lv_style_init(&s_green_style.main);
    lv_style_set_arc_color(&s_green_style.main, lv_color_hex(GAUGE_GREEN));
    lv_style_set_arc_width(&s_green_style.main, 18);

    s_section_styles_ready = true;
}

static int32_t watts_to_scale(float watts)
{
    if (watts <= 0.0f) {
        return 0;
    }
    return (int32_t)lroundf(watts);
}

static void add_gauge_section(lv_obj_t *scale, int32_t from, int32_t to, const gauge_section_styles_t *styles)
{
    if (from >= to) {
        return;
    }

    lv_scale_section_t *section = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, section, from, to);
    lv_scale_set_section_style_items(scale, section, &styles->items);
    lv_scale_set_section_style_indicator(scale, section, &styles->indicator);
    lv_scale_set_section_style_main(scale, section, &styles->main);
}

static void destroy_gauge(tile_filter_watts_t *tile)
{
    if (tile == NULL || tile->gauge_container == NULL) {
        return;
    }

    lv_obj_clean(tile->gauge_container);
    tile->scale = NULL;
    tile->needle = NULL;
    tile->value_label = NULL;
    s_built_baseline = 0.0f;
    s_built_green_pct = 0;
    s_built_yellow_pct = 0;
    s_built_red_pct = 0;
    s_built_scale_max = 0;
}

static bool gauge_config_matches(const tile_filter_watts_t *tile, float baseline, uint8_t green_pct,
                                 uint8_t yellow_pct, uint8_t red_pct, int32_t scale_max)
{
    return tile != NULL && tile->scale != NULL && s_built_baseline == baseline && s_built_green_pct == green_pct &&
           s_built_yellow_pct == yellow_pct && s_built_red_pct == red_pct && s_built_scale_max == scale_max;
}

static bool build_gauge(tile_filter_watts_t *tile, float baseline, uint8_t green_pct, uint8_t yellow_pct,
                        uint8_t red_pct, float max_watts)
{
    if (tile == NULL || tile->gauge_container == NULL) {
        return false;
    }

    const int32_t scale_max = watts_to_scale(max_watts);
    if (gauge_config_matches(tile, baseline, green_pct, yellow_pct, red_pct, scale_max)) {
        return true;
    }

    destroy_gauge(tile);
    init_section_styles();

    const float green_lo = baseline * (100.0f - (float)green_pct) / 100.0f;
    const float green_hi = baseline * (100.0f + (float)green_pct) / 100.0f;
    const float yellow_lo = baseline * (100.0f - (float)yellow_pct) / 100.0f;
    const float yellow_hi = baseline * (100.0f + (float)yellow_pct) / 100.0f;

    tile->scale = lv_scale_create(tile->gauge_container);
    lv_obj_set_size(tile->scale, LV_PCT(100), LV_PCT(100));
    lv_obj_center(tile->scale);
    lv_scale_set_mode(tile->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(tile->scale, 0, scale_max);
    lv_scale_set_total_tick_count(tile->scale, 1);
    lv_scale_set_label_show(tile->scale, false);
    lv_scale_set_angle_range(tile->scale, 270);
    lv_scale_set_rotation(tile->scale, 135);
    lv_obj_set_style_arc_width(tile->scale, 0, LV_PART_MAIN);
    lv_obj_set_style_length(tile->scale, 0, LV_PART_ITEMS);
    lv_obj_set_style_length(tile->scale, 0, LV_PART_INDICATOR);

    add_gauge_section(tile->scale, 0, watts_to_scale(yellow_lo), &s_red_style);
    add_gauge_section(tile->scale, watts_to_scale(yellow_lo), watts_to_scale(green_lo), &s_yellow_style);
    add_gauge_section(tile->scale, watts_to_scale(green_lo), watts_to_scale(green_hi), &s_green_style);
    add_gauge_section(tile->scale, watts_to_scale(green_hi), watts_to_scale(yellow_hi), &s_yellow_style);
    add_gauge_section(tile->scale, watts_to_scale(yellow_hi), scale_max, &s_red_style);

    tile->needle = lv_line_create(tile->scale);
    lv_obj_set_style_line_color(tile->needle, lv_color_hex(TILE_VALUE_COLOR), LV_PART_MAIN);
    lv_obj_set_style_line_width(tile->needle, 8, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(tile->needle, true, LV_PART_MAIN);

    tile->value_label = lv_label_create(tile->gauge_container);
    lv_obj_set_style_text_color(tile->value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile->value_label, &lv_font_montserrat_24, 0);
    lv_obj_center(tile->value_label);

    s_built_baseline = baseline;
    s_built_green_pct = green_pct;
    s_built_yellow_pct = yellow_pct;
    s_built_red_pct = red_pct;
    s_built_scale_max = scale_max;
    return true;
}

static uint32_t zone_color(filter_power_zone_t zone)
{
    switch (zone) {
    case FILTER_POWER_ZONE_GREEN:
        return GAUGE_GREEN;
    case FILTER_POWER_ZONE_YELLOW:
        return GAUGE_YELLOW;
    case FILTER_POWER_ZONE_RED:
        return GAUGE_RED;
    default:
        return TILE_VALUE_COLOR;
    }
}

tile_filter_watts_t tile_filter_watts_create(lv_obj_t *parent)
{
    tile_filter_watts_t tile = {0};

    tile.root = lv_obj_create(parent);
    tile_apply_panel_style(tile.root);
    lv_obj_set_size(tile.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.root, 4, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Filter Watts");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.gauge_container = lv_obj_create(tile.root);
    lv_obj_remove_style_all(tile.gauge_container);
    lv_obj_set_width(tile.gauge_container, LV_PCT(100));
    lv_obj_set_flex_grow(tile.gauge_container, 1);
    lv_obj_remove_flag(tile.gauge_container, LV_OBJ_FLAG_SCROLLABLE);

    tile.hint_label = lv_label_create(tile.root);
    lv_label_set_text(tile.hint_label, "Calibrate filter\nfor gauge bands");
    lv_obj_set_style_text_color(tile.hint_label, lv_color_hex(TILE_HINT_COLOR), 0);
    lv_obj_set_style_text_font(tile.hint_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(tile.hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(tile.hint_label, LV_OBJ_FLAG_HIDDEN);

    tile_filter_watts_update(&tile);
    return tile;
}

void tile_filter_watts_update(tile_filter_watts_t *tile)
{
    if (tile == NULL) {
        return;
    }

    uint16_t watts = 0;
    const bool have_watts = filter_power_monitor_get_watts(&watts);

    float baseline = 0.0f;
    uint8_t green_pct = 0;
    uint8_t yellow_pct = 0;
    uint8_t red_pct = 0;
    const bool calibrated = filter_power_monitor_get_baseline_watts(&baseline) &&
                            aquapilot_settings_get_filter_bands(&green_pct, &yellow_pct, &red_pct);

    if (!calibrated) {
        destroy_gauge(tile);
        if (tile->hint_label != NULL) {
            lv_obj_remove_flag(tile->hint_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(tile->hint_label);
            if (have_watts) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%u W\nCalibrate for gauge", (unsigned)watts);
                lv_label_set_text(tile->hint_label, buf);
            } else {
                lv_label_set_text(tile->hint_label, "Calibrate filter\nfor gauge bands");
            }
        }
        return;
    }

    if (tile->hint_label != NULL) {
        lv_obj_add_flag(tile->hint_label, LV_OBJ_FLAG_HIDDEN);
    }

    float max_watts = baseline;
    if (!filter_power_monitor_get_gauge_max_watts(&max_watts)) {
        max_watts = baseline * 1.5f;
    }

    if (!build_gauge(tile, baseline, green_pct, yellow_pct, red_pct, max_watts)) {
        return;
    }

    const int32_t needle_value = have_watts ? watts_to_scale((float)watts) : 0;
    if (tile->needle != NULL && tile->scale != NULL) {
        lv_scale_set_line_needle_value(tile->scale, tile->needle, 42, needle_value);
        const filter_power_zone_t zone = filter_power_monitor_get_zone();
        lv_obj_set_style_line_color(tile->needle, lv_color_hex(zone_color(zone)), LV_PART_MAIN);
    }

    if (tile->value_label != NULL) {
        char buf[16];
        if (have_watts) {
            snprintf(buf, sizeof(buf), "%u W", (unsigned)watts);
        } else {
            snprintf(buf, sizeof(buf), "-- W");
        }
        lv_label_set_text(tile->value_label, buf);
        lv_obj_set_style_text_color(tile->value_label, lv_color_hex(zone_color(filter_power_monitor_get_zone())), 0);
    }
}

bool tile_filter_watts_needs_fast_update(void)
{
    return filter_power_monitor_is_calibrated();
}
