#include "aquapilot_ui.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "esp_log.h"
#include "lvgl.h"
#include "net/wifi_manager.h"
#include "storage/aquapilot_nvs.h"
#include "storage/aquapilot_settings.h"
#include "schedule/co2_schedule.h"
#include "schedule/co2_automation.h"
#include "schedule/feeder_service.h"
#include "schedule/aquapilot_time.h"
#include "heater/heater_service.h"
#include "light/light_service.h"
#include "safety/heater_override.h"
#include "safety/co2_power_monitor.h"
#include "safety/filter_power_monitor.h"
#include "feeder/feeder_client.h"
#include "safety/filter_calibration.h"
#include "safety/maintenance_mode.h"
#include "safety/equipment_restore.h"
#include "display/display_control.h"
#include "sensors/sht3x_sensor.h"
#include "storage/sd_storage.h"
#include "storage/temp_history.h"

static const char *TAG = "aquapilot";

static lv_display_t *display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start_with_config() failed");
        return NULL;
    }

    ESP_LOGI(TAG, "display: %dx%d, draw buffer %u px, PSRAM=%d",
             BSP_LCD_H_RES, BSP_LCD_V_RES,
             (unsigned)BSP_LCD_DRAW_BUFF_SIZE,
             cfg.flags.buff_spiram);

    return disp;
}

static bool platform_storage_init(void)
{
    esp_err_t nvs_err = aquapilot_nvs_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
        return false;
    }

    aquapilot_settings_init();
    return true;
}

static void platform_init(void)
{
    esp_err_t maint_err = maintenance_mode_init();
    if (maint_err != ESP_OK) {
        ESP_LOGW(TAG, "maintenance mode init failed");
    }

    aquapilot_time_init();
    co2_schedule_init();

    if (!aquapilot_wifi_init()) {
        ESP_LOGW(TAG, "Wi-Fi stack init failed (UI will still run)");
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi stack ready (STA deferred until heater BLE setup)");
}

void app_main(void)
{
    ESP_LOGI(TAG, "AquaPilot starting");

    if (!platform_storage_init()) {
        return;
    }

    /* Complete SD mount before BLE/Wi-Fi SDIO traffic ramps up. */
    if (!aquapilot_sd_ensure_mounted(120000)) {
        ESP_LOGW(TAG, "SD card not available at boot");
    }

    esp_err_t hist_err = temp_history_init();
    if (hist_err != ESP_OK) {
        ESP_LOGW(TAG, "temperature history init failed");
    }

    esp_err_t heater_err = heater_service_init();
    if (heater_err != ESP_OK) {
        ESP_LOGW(TAG, "heater BLE init failed (temperature tile will wait)");
    }

    esp_err_t light_err = light_service_init();
    if (light_err != ESP_OK) {
        ESP_LOGW(TAG, "light BLE init failed (light tile will wait)");
    }

    platform_init();

    esp_err_t override_err = heater_override_init();
    if (override_err != ESP_OK) {
        ESP_LOGW(TAG, "heater override init failed");
    }

    esp_err_t co2_auto_err = co2_automation_init();
    if (co2_auto_err != ESP_OK) {
        ESP_LOGW(TAG, "CO2 automation init failed");
    }

    esp_err_t feeder_err = feeder_service_init();
    if (feeder_err != ESP_OK) {
        ESP_LOGW(TAG, "feeder service init failed");
    }

    esp_err_t feeder_client_err = feeder_client_init();
    if (feeder_client_err != ESP_OK) {
        ESP_LOGW(TAG, "feeder client init failed");
    }

    esp_err_t co2_power_err = co2_power_monitor_init();
    if (co2_power_err != ESP_OK) {
        ESP_LOGW(TAG, "CO2 power monitor init failed");
    }

    esp_err_t filter_power_err = filter_power_monitor_init();
    if (filter_power_err != ESP_OK) {
        ESP_LOGW(TAG, "filter power monitor init failed");
    }

    esp_err_t filter_cal_err = filter_calibration_init();
    if (filter_cal_err != ESP_OK) {
        ESP_LOGW(TAG, "filter calibration init failed");
    }

    esp_err_t restore_err = equipment_restore_init();
    if (restore_err != ESP_OK) {
        ESP_LOGW(TAG, "equipment restore init failed");
    }

    if (heater_err != ESP_OK) {
        if (aquapilot_wifi_start_sta()) {
            ESP_LOGI(TAG, "Wi-Fi STA started (heater unavailable)");
        }
    }

    lv_display_t *disp = display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display init failed — halting (check wiring and BSP config)");
        return;
    }

    display_control_init(disp);
    display_control_apply_saved_settings();

    esp_err_t sht_err = sht3x_sensor_init();
    if (sht_err != ESP_OK) {
        ESP_LOGW(TAG, "SHT3x init failed (ambient tile will show --)");
    }

    if (!bsp_display_lock(5000)) {
        ESP_LOGE(TAG, "display lock timeout before UI init");
        return;
    }

    aquapilot_ui_init();
    bsp_display_unlock();

    ESP_LOGI(TAG, "ready — 3x3 dashboard visible");
}
