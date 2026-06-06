#include "screen_main.h"

#include "tile_co2_schedule.h"
#include "tile_co2_status.h"
#include "tile_common.h"
#include "tile_connections.h"
#include "tile_filter_status.h"
#include "tile_filter_watts.h"
#include "tile_heater_power.h"
#include "tile_light_status.h"
#include "tile_settings.h"
#include "tile_temp.h"
#include "ui_nav.h"

#define SCREEN_BG_COLOR 0x0D1117

static lv_obj_t *s_screen;
static tile_filter_status_t s_filter_status_tile;
static tile_filter_watts_t s_filter_watts_tile;
static tile_co2_status_t s_co2_status_tile;
static tile_co2_schedule_t s_co2_schedule_tile;
static tile_temp_t s_temp_tile;
static tile_heater_power_t s_heater_power_tile;
static tile_connections_t s_connections_tile;
static tile_light_status_t s_light_status_tile;

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    tile_co2_status_update(&s_co2_status_tile);
    tile_co2_schedule_update(&s_co2_schedule_tile);
    tile_temp_update(&s_temp_tile);
    tile_heater_power_update(&s_heater_power_tile);
    tile_filter_status_update(&s_filter_status_tile);
    tile_filter_watts_update(&s_filter_watts_tile);
    tile_light_status_update(&s_light_status_tile);
    tile_connections_update(&s_connections_tile);
}

static void heater_power_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (tile_heater_power_needs_fast_update()) {
        tile_heater_power_update(&s_heater_power_tile);
    }
}

static void co2_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (tile_co2_status_needs_fast_update()) {
        tile_co2_status_update(&s_co2_status_tile);
    }
}

static void filter_watts_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (tile_filter_watts_needs_fast_update()) {
        tile_filter_watts_update(&s_filter_watts_tile);
        tile_filter_status_update(&s_filter_status_tile);
    }
}

static void grid_add_cell(lv_obj_t *grid, lv_obj_t *cell, int col, int row)
{
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
}

static void add_empty_cell(lv_obj_t *grid, int col, int row)
{
    lv_obj_t *empty = tile_empty_create(grid);
    grid_add_cell(grid, empty, col, row);
}

screen_main_t screen_main_create(void)
{
    screen_main_t main_screen = {0};

    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(SCREEN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 24, 0);

    lv_obj_t *grid = lv_obj_create(s_screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 12, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    s_co2_status_tile = tile_co2_status_create(grid);
    grid_add_cell(grid, s_co2_status_tile.root, 0, 0);

    s_temp_tile = tile_temp_create(grid);
    grid_add_cell(grid, s_temp_tile.root, 1, 0);

    s_filter_status_tile = tile_filter_status_create(grid);
    grid_add_cell(grid, s_filter_status_tile.root, 2, 0);

    s_co2_schedule_tile = tile_co2_schedule_create(grid);
    grid_add_cell(grid, s_co2_schedule_tile.root, 0, 1);

    s_heater_power_tile = tile_heater_power_create(grid);
    grid_add_cell(grid, s_heater_power_tile.root, 1, 1);

    s_filter_watts_tile = tile_filter_watts_create(grid);
    grid_add_cell(grid, s_filter_watts_tile.root, 2, 1);

    s_light_status_tile = tile_light_status_create(grid);
    grid_add_cell(grid, s_light_status_tile.root, 0, 2);

    s_connections_tile = tile_connections_create(grid);
    grid_add_cell(grid, s_connections_tile.root, 1, 2);

    tile_settings_t settings = tile_settings_create(grid);
    grid_add_cell(grid, settings.root, 2, 2);

    ui_nav_set_dashboard_screen(s_screen);
    lv_screen_load(s_screen);

    main_screen.screen = s_screen;
    main_screen.update_timer = lv_timer_create(update_timer_cb, 3000, NULL);
    lv_timer_create(heater_power_timer_cb, 500, NULL);
    lv_timer_create(co2_status_timer_cb, 500, NULL);
    lv_timer_create(filter_watts_timer_cb, 500, NULL);

    return main_screen;
}
