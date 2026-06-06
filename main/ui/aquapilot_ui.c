#include "aquapilot_ui.h"

#include "esp_log.h"
#include "screen_main.h"
#include "screen_settings.h"
#include "screen_wifi.h"
static const char *TAG = "aquapilot_ui";

void aquapilot_ui_init(void)
{
    screen_settings_create();
    screen_wifi_create();
    screen_main_create();
    ESP_LOGI(TAG, "dashboard (3x3 grid) and settings ready");
}
