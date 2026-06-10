#include "maintenance_mode.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi_manager.h"
#include "safety/equipment_restore.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "maint_mode";

#define STEP_DELAY_MS 5000

static bool s_active;
static bool s_running;
static char s_status[96] = "Idle";

static void set_status(const char *text)
{
    if (text == NULL) {
        return;
    }
    snprintf(s_status, sizeof(s_status), "%s", text);
}

static void delay_step(void)
{
    vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
}

static bool shelly_set_if_configured(aquapilot_shelly_plug_t plug, bool on)
{
    if (!aquapilot_settings_has_shelly_address(plug)) {
        ESP_LOGW(TAG, "plug %d not configured, skipping", (int)plug);
        return false;
    }
    if (!aquapilot_wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi offline, cannot set plug %d", (int)plug);
        return false;
    }

    return equipment_set_plug_desired(plug, on);
}

static void run_enable_sequence(void)
{
    set_status("Turning off heater plug...");
    (void)shelly_set_if_configured(AQUAPILOT_SHELLY_HEATER, false);
    delay_step();

    set_status("Turning off CO2 plug...");
    (void)shelly_set_if_configured(AQUAPILOT_SHELLY_CO2, false);
    delay_step();

    set_status("Turning off filter plug...");
    (void)shelly_set_if_configured(AQUAPILOT_SHELLY_FILTER, false);
    set_status("Maintenance mode active");
}

static void run_disable_sequence(void)
{
    (void)equipment_apply_normal_state(set_status, true);
    set_status("Maintenance mode disabled");
}

static void sequence_task(void *arg)
{
    const bool enable = (bool)(uintptr_t)arg;

    s_running = true;
    if (enable) {
        s_active = true;
        run_enable_sequence();
    } else {
        run_disable_sequence();
        s_active = false;
    }
    s_running = false;
    vTaskDelete(NULL);
}

esp_err_t maintenance_mode_init(void)
{
    bool enabled = false;
    if (!aquapilot_settings_get_maintenance_mode_enabled(&enabled)) {
        enabled = false;
    }
    s_active = enabled;
    snprintf(s_status, sizeof(s_status), "%s", enabled ? "Maintenance mode active" : "Idle");
    return ESP_OK;
}

bool maintenance_mode_is_active(void)
{
    return s_active;
}

bool maintenance_mode_sequence_running(void)
{
    return s_running;
}

const char *maintenance_mode_status_text(void)
{
    return s_status;
}

esp_err_t maintenance_mode_apply(bool enabled)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!aquapilot_settings_set_maintenance_mode_enabled(enabled)) {
        return ESP_FAIL;
    }

    if (xTaskCreate(sequence_task, "maint_mode", 4096, (void *)(uintptr_t)enabled, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start maintenance sequence task");
        return ESP_FAIL;
    }

    set_status(enabled ? "Starting maintenance shutdown..." : "Restoring equipment...");
    return ESP_OK;
}
