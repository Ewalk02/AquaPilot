#include "display_control.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "esp_log.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "display_ctrl";

static lv_display_t *s_disp;

void display_control_init(lv_display_t *disp)
{
    s_disp = disp;
}

void display_control_set_flip_180(bool flip)
{
    if (s_disp == NULL) {
        return;
    }

    if (!bsp_display_lock(1000)) {
        ESP_LOGW(TAG, "display lock timeout applying rotation");
        return;
    }

    bsp_display_rotate(s_disp, flip ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);

    lv_obj_t *screen = lv_screen_active();
    if (screen != NULL) {
        lv_obj_invalidate(screen);
    }
    lv_refr_now(s_disp);

    bsp_display_unlock();
    ESP_LOGI(TAG, "display rotation %s", flip ? "180°" : "0°");
}

void display_control_set_brightness(uint8_t brightness_pct)
{
    if (brightness_pct > 100) {
        brightness_pct = 100;
    }

    const esp_err_t err = bsp_display_brightness_set((int)brightness_pct);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "brightness set failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "brightness %u%%", (unsigned)brightness_pct);
}

void display_control_apply_saved_settings(void)
{
    bool flip = false;
    uint8_t brightness = 100;

    aquapilot_settings_get_display_flip_180(&flip);
    aquapilot_settings_get_display_brightness(&brightness);

    display_control_set_brightness(brightness);
    display_control_set_flip_180(flip);
}
