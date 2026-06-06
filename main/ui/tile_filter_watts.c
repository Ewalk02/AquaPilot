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
#define MARKER_GRAY      0xC9D1D9
#define BAR_HEIGHT       24
#define BAR_WIDTH_PCT    96
#define SEG_BORDER_W     1
#define MARKER_WIDTH     6
#define MARKER_HEIGHT    40
#define BORDER_SUBTLE    0x30363D
#define SCALE_LABEL_H    16
#define SCALE_LABEL_GAP  4
#define FILTER_DEVICE_NAME     "Fluval 407"
#define DEVICE_LABEL_MARGIN    4
#define MAX_SCALE_TICKS  6
#define MAX_BANDS        5

typedef struct {
    float from_w;
    float to_w;
    uint32_t color;
} band_def_t;

static float s_built_baseline;
static uint8_t s_built_green_pct;
static uint8_t s_built_yellow_pct;
static uint8_t s_built_red_pct;
static uint8_t s_built_red_cutoff_pct;
static float s_built_min_watts;
static float s_built_max_watts;
static float s_band_yellow_lo;
static float s_band_green_lo;
static float s_band_green_hi;
static float s_band_yellow_hi;
static lv_obj_t *s_tick_labels[MAX_SCALE_TICKS];
static float s_tick_watts[MAX_SCALE_TICKS];
static uint8_t s_tick_count;

static int32_t watts_to_label(float watts)
{
    if (watts <= 0.0f) {
        return 0;
    }
    return (int32_t)lroundf(watts);
}

static lv_coord_t watt_to_x(float watts, lv_coord_t track_width, float min_watts, float max_watts)
{
    if (max_watts <= min_watts || track_width <= 0) {
        return 0;
    }

    float clamped = watts;
    if (clamped < min_watts) {
        clamped = min_watts;
    }
    if (clamped > max_watts) {
        clamped = max_watts;
    }

    return (lv_coord_t)lroundf(((clamped - min_watts) / (max_watts - min_watts)) * (float)track_width);
}

static lv_coord_t track_content_width(lv_obj_t *bar_row)
{
    if (bar_row == NULL) {
        return 0;
    }

    lv_obj_update_layout(bar_row);
    lv_coord_t width = lv_obj_get_content_width(bar_row);
    if (width <= 0) {
        width = lv_obj_get_width(bar_row);
    }
    return width;
}

static lv_coord_t track_border_inset(lv_obj_t *bar_row)
{
    if (bar_row == NULL) {
        return 0;
    }
    return lv_obj_get_style_border_width(bar_row, LV_PART_MAIN);
}

static void clear_tick_labels(void)
{
    for (uint8_t i = 0; i < s_tick_count; i++) {
        s_tick_labels[i] = NULL;
        s_tick_watts[i] = 0.0f;
    }
    s_tick_count = 0;
}

static void destroy_gauge(tile_filter_watts_t *tile)
{
    if (tile == NULL) {
        return;
    }

    if (tile->scale_labels != NULL) {
        clear_tick_labels();
        lv_obj_delete(tile->scale_labels);
        tile->scale_labels = NULL;
    }
    if (tile->bar_row != NULL) {
        lv_obj_delete(tile->bar_row);
        tile->bar_row = NULL;
    }
    if (tile->marker != NULL) {
        lv_obj_delete(tile->marker);
        tile->marker = NULL;
    }
    if (tile->value_label != NULL) {
        lv_obj_delete(tile->value_label);
        tile->value_label = NULL;
    }

    s_built_baseline = 0.0f;
    s_built_green_pct = 0;
    s_built_yellow_pct = 0;
    s_built_red_pct = 0;
    s_built_red_cutoff_pct = 0;
    s_built_min_watts = 0.0f;
    s_built_max_watts = 0.0f;
}

static bool gauge_config_matches(const tile_filter_watts_t *tile, float baseline, uint8_t green_pct,
                                 uint8_t yellow_pct, uint8_t red_pct, uint8_t red_cutoff_pct, float min_watts,
                                 float max_watts)
{
    return tile != NULL && tile->bar_row != NULL && s_built_baseline == baseline && s_built_green_pct == green_pct &&
           s_built_yellow_pct == yellow_pct && s_built_red_pct == red_pct &&
           s_built_red_cutoff_pct == red_cutoff_pct && s_built_min_watts == min_watts &&
           s_built_max_watts == max_watts;
}

static void update_marker(tile_filter_watts_t *tile, float watts_value);

static void layout_band_segments(tile_filter_watts_t *tile)
{
    if (tile == NULL || tile->bar_row == NULL || s_built_max_watts <= s_built_min_watts) {
        return;
    }

    lv_obj_set_width(tile->bar_row, LV_PCT(BAR_WIDTH_PCT));
    lv_obj_set_height(tile->bar_row, BAR_HEIGHT);
    lv_obj_set_style_radius(tile->bar_row, 4, 0);
    lv_obj_set_style_border_color(tile->bar_row, lv_color_hex(BORDER_SUBTLE), 0);
    lv_obj_set_style_border_width(tile->bar_row, SEG_BORDER_W, 0);
    lv_obj_set_style_clip_corner(tile->bar_row, true, 0);

    lv_obj_update_layout(tile->bar_row);

    const lv_coord_t track_w = track_content_width(tile->bar_row);
    if (track_w <= 0) {
        return;
    }

    lv_coord_t bar_h = lv_obj_get_content_height(tile->bar_row);
    if (bar_h <= 0) {
        bar_h = BAR_HEIGHT;
    }

    lv_obj_clean(tile->bar_row);

    const band_def_t bands[MAX_BANDS] = {
        {s_built_min_watts, s_band_yellow_lo, GAUGE_RED},
        {s_band_yellow_lo, s_band_green_lo, GAUGE_YELLOW},
        {s_band_green_lo, s_band_green_hi, GAUGE_GREEN},
        {s_band_green_hi, s_band_yellow_hi, GAUGE_YELLOW},
        {s_band_yellow_hi, s_built_max_watts, GAUGE_RED},
    };

    for (size_t i = 0; i < MAX_BANDS; i++) {
        const lv_coord_t x0 = watt_to_x(bands[i].from_w, track_w, s_built_min_watts, s_built_max_watts);
        const lv_coord_t x1 = watt_to_x(bands[i].to_w, track_w, s_built_min_watts, s_built_max_watts);
        const lv_coord_t seg_w = x1 - x0;
        if (seg_w < 1) {
            continue;
        }

        lv_obj_t *seg = lv_obj_create(tile->bar_row);
        lv_obj_remove_style_all(seg);
        lv_obj_set_pos(seg, x0, 0);
        lv_obj_set_size(seg, seg_w, bar_h);
        lv_obj_set_style_bg_color(seg, lv_color_hex(bands[i].color), 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void position_scale_label(lv_obj_t *label, lv_obj_t *bar_row, lv_obj_t *scale_labels, float watts,
                                 bool first, bool last)
{
    if (label == NULL || bar_row == NULL || scale_labels == NULL || s_built_max_watts <= s_built_min_watts) {
        return;
    }

    const lv_coord_t track_w = track_content_width(bar_row);
    if (track_w <= 0) {
        return;
    }

    const lv_coord_t inset = track_border_inset(bar_row);

    lv_obj_update_layout(label);
    const lv_coord_t label_w = lv_obj_get_width(label);
    lv_coord_t x = watt_to_x(watts, track_w, s_built_min_watts, s_built_max_watts);

    if (first) {
        x = 0;
    } else if (last) {
        x = track_w - label_w;
        if (x < 0) {
            x = 0;
        }
    } else if (label_w > 0) {
        x -= label_w / 2;
        if (x < 0) {
            x = 0;
        }
        if (x > track_w - label_w) {
            x = track_w - label_w;
        }
    }

    lv_obj_set_pos(label, inset + x, SCALE_LABEL_GAP);
}

static void layout_scale_labels(tile_filter_watts_t *tile)
{
    if (tile == NULL || tile->bar_row == NULL || tile->scale_labels == NULL || s_tick_count == 0) {
        return;
    }

    for (uint8_t i = 0; i < s_tick_count; i++) {
        if (s_tick_labels[i] == NULL) {
            continue;
        }

        const bool first = (i == 0);
        const bool last = (i + 1U == s_tick_count);
        position_scale_label(s_tick_labels[i], tile->bar_row, tile->scale_labels, s_tick_watts[i], first, last);
    }
}

static void build_scale_labels(tile_filter_watts_t *tile, float min_watts, float max_watts, float yellow_lo,
                               float green_lo, float green_hi, float yellow_hi)
{
    if (tile == NULL || tile->bar_row == NULL || tile->scale_labels == NULL) {
        return;
    }

    clear_tick_labels();

    const float boundaries[] = {min_watts, yellow_lo, green_lo, green_hi, yellow_hi, max_watts};
    int32_t last_value = -1;

    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i++) {
        if (s_tick_count >= MAX_SCALE_TICKS) {
            break;
        }

        const int32_t tick_value = watts_to_label(boundaries[i]);
        if (tick_value == last_value) {
            continue;
        }
        last_value = tick_value;

        lv_obj_t *label = lv_label_create(tile->scale_labels);
        char buf[8];
        snprintf(buf, sizeof(buf), "%ld", (long)tick_value);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_color(label, lv_color_hex(TILE_HINT_COLOR), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);

        s_tick_labels[s_tick_count] = label;
        s_tick_watts[s_tick_count] = boundaries[i];
        s_tick_count++;
    }
}

static void layout_gauge_geometry(tile_filter_watts_t *tile, float marker_watts)
{
    layout_band_segments(tile);
    update_marker(tile, marker_watts);
    layout_scale_labels(tile);
}

static bool build_gauge(tile_filter_watts_t *tile, float baseline, uint8_t green_pct, uint8_t yellow_pct,
                        uint8_t red_pct, uint8_t red_cutoff_pct, float min_watts, float max_watts)
{
    if (tile == NULL || tile->gauge_container == NULL) {
        return false;
    }

    if (gauge_config_matches(tile, baseline, green_pct, yellow_pct, red_pct, red_cutoff_pct, min_watts, max_watts)) {
        return true;
    }

    destroy_gauge(tile);

    s_band_yellow_lo = baseline * (100.0f - (float)yellow_pct) / 100.0f;
    s_band_green_lo = baseline * (100.0f - (float)green_pct) / 100.0f;
    s_band_green_hi = baseline * (100.0f + (float)green_pct) / 100.0f;
    s_band_yellow_hi = baseline * (100.0f + (float)yellow_pct) / 100.0f;

    tile->value_label = lv_label_create(tile->gauge_container);
    lv_obj_set_style_text_color(tile->value_label, lv_color_hex(TILE_VALUE_COLOR), 0);
    lv_obj_set_style_text_font(tile->value_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(tile->value_label, "-- W");

    tile->bar_row = lv_obj_create(tile->gauge_container);
    lv_obj_remove_style_all(tile->bar_row);
    lv_obj_set_width(tile->bar_row, LV_PCT(BAR_WIDTH_PCT));
    lv_obj_set_height(tile->bar_row, BAR_HEIGHT);
    lv_obj_remove_flag(tile->bar_row, LV_OBJ_FLAG_SCROLLABLE);

    tile->scale_labels = lv_obj_create(tile->gauge_container);
    lv_obj_remove_style_all(tile->scale_labels);
    lv_obj_set_width(tile->scale_labels, LV_PCT(BAR_WIDTH_PCT));
    lv_obj_set_height(tile->scale_labels, SCALE_LABEL_H + SCALE_LABEL_GAP);
    lv_obj_add_flag(tile->scale_labels, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(tile->scale_labels, LV_OBJ_FLAG_SCROLLABLE);

    build_scale_labels(tile, min_watts, max_watts, s_band_yellow_lo, s_band_green_lo, s_band_green_hi, s_band_yellow_hi);

    if (tile->device_label == NULL) {
        tile->device_label = lv_label_create(tile->gauge_container);
        lv_label_set_text(tile->device_label, FILTER_DEVICE_NAME);
        lv_obj_set_style_text_color(tile->device_label, lv_color_hex(TILE_HINT_COLOR), 0);
        lv_obj_set_style_text_font(tile->device_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(tile->device_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_margin_top(tile->device_label, DEVICE_LABEL_MARGIN, 0);
    }
    lv_obj_remove_flag(tile->device_label, LV_OBJ_FLAG_HIDDEN);

    tile->marker = lv_obj_create(tile->gauge_container);
    lv_obj_remove_style_all(tile->marker);
    lv_obj_set_size(tile->marker, MARKER_WIDTH, MARKER_HEIGHT);
    lv_obj_set_style_bg_color(tile->marker, lv_color_hex(MARKER_GRAY), 0);
    lv_obj_set_style_bg_opa(tile->marker, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile->marker, 2, 0);
    lv_obj_set_style_border_color(tile->marker, lv_color_hex(BORDER_SUBTLE), 0);
    lv_obj_set_style_border_width(tile->marker, SEG_BORDER_W, 0);
    lv_obj_add_flag(tile->marker, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(tile->marker, LV_OBJ_FLAG_SCROLLABLE);

    s_built_baseline = baseline;
    s_built_green_pct = green_pct;
    s_built_yellow_pct = yellow_pct;
    s_built_red_pct = red_pct;
    s_built_red_cutoff_pct = red_cutoff_pct;
    s_built_min_watts = min_watts;
    s_built_max_watts = max_watts;

    return true;
}

static void update_marker(tile_filter_watts_t *tile, float watts_value)
{
    if (tile == NULL || tile->bar_row == NULL || tile->marker == NULL || s_built_max_watts <= s_built_min_watts) {
        return;
    }

    const lv_coord_t track_w = track_content_width(tile->bar_row);
    if (track_w <= 0) {
        return;
    }

    const lv_coord_t inset = track_border_inset(tile->bar_row);
    lv_coord_t marker_x = inset + watt_to_x(watts_value, track_w, s_built_min_watts, s_built_max_watts);
    marker_x -= MARKER_WIDTH / 2;
    if (marker_x < inset) {
        marker_x = inset;
    }
    if (marker_x > inset + track_w - MARKER_WIDTH) {
        marker_x = inset + track_w - MARKER_WIDTH;
    }

    lv_obj_align_to(tile->marker, tile->bar_row, LV_ALIGN_LEFT_MID, marker_x, 0);
    lv_obj_move_foreground(tile->marker);
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
    lv_obj_set_style_pad_bottom(tile.root, 4, 0);
    lv_obj_remove_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile.root);
    lv_label_set_text(title, "Filter Power");
    lv_obj_set_style_text_color(title, lv_color_hex(TILE_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    tile.gauge_container = lv_obj_create(tile.root);
    lv_obj_remove_style_all(tile.gauge_container);
    lv_obj_set_width(tile.gauge_container, LV_PCT(100));
    lv_obj_set_flex_grow(tile.gauge_container, 1);
    lv_obj_set_layout(tile.gauge_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile.gauge_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile.gauge_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile.gauge_container, 4, 0);
    lv_obj_set_style_pad_top(tile.gauge_container, 0, 0);
    lv_obj_set_style_pad_bottom(tile.gauge_container, 2, 0);
    lv_obj_add_flag(tile.gauge_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(tile.gauge_container, LV_OBJ_FLAG_SCROLLABLE);

    tile.hint_label = lv_label_create(tile.gauge_container);
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
    uint8_t red_cutoff_pct = 0;
    const bool calibrated = filter_power_monitor_is_calibrated() &&
                            aquapilot_settings_get_filter_bands(&green_pct, &yellow_pct, &red_pct, &red_cutoff_pct);

    if (!calibrated) {
        destroy_gauge(tile);
        if (tile->device_label != NULL) {
            lv_obj_add_flag(tile->device_label, LV_OBJ_FLAG_HIDDEN);
        }
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

    if (!filter_power_monitor_get_baseline_watts(&baseline)) {
        return;
    }

    float min_watts = 0.0f;
    float max_watts = baseline;
    if (!filter_power_monitor_get_gauge_range(&min_watts, &max_watts)) {
        max_watts = baseline * 1.5f;
        min_watts = 0.0f;
    }

    if (!build_gauge(tile, baseline, green_pct, yellow_pct, red_pct, red_cutoff_pct, min_watts, max_watts)) {
        return;
    }

    const filter_power_zone_t zone = filter_power_monitor_get_zone();
    const float marker_watts = have_watts ? (float)watts : 0.0f;

    lv_obj_update_layout(tile->gauge_container);
    layout_gauge_geometry(tile, marker_watts);

    if (tile->value_label != NULL) {
        char buf[16];
        if (have_watts) {
            snprintf(buf, sizeof(buf), "%u W", (unsigned)watts);
        } else {
            snprintf(buf, sizeof(buf), "-- W");
        }
        lv_label_set_text(tile->value_label, buf);
        lv_obj_set_style_text_color(tile->value_label, lv_color_hex(zone_color(zone)), 0);
    }
}

bool tile_filter_watts_needs_fast_update(void)
{
    return filter_power_monitor_is_calibrated();
}
