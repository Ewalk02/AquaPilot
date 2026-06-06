#include "ui_nav.h"

static lv_obj_t *s_dashboard_screen;
static lv_obj_t *s_settings_screen;

void ui_nav_set_dashboard_screen(lv_obj_t *screen)
{
    s_dashboard_screen = screen;
}

void ui_nav_set_settings_screen(lv_obj_t *screen)
{
    s_settings_screen = screen;
}

void ui_nav_show_dashboard(void)
{
    if (s_dashboard_screen != NULL) {
        lv_screen_load(s_dashboard_screen);
    }
}

void ui_nav_show_settings(void)
{
    if (s_settings_screen != NULL) {
        lv_screen_load(s_settings_screen);
    }
}
