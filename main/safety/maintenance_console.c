#include "maintenance_console.h"

#include <stdio.h>

#include "esp_console.h"
#include "esp_log.h"
#include "net/shelly_client.h"
#include "safety/maintenance_mode.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "maint_console";

static const char *plug_label(aquapilot_shelly_plug_t plug)
{
    switch (plug) {
    case AQUAPILOT_SHELLY_HEATER:
        return "heater";
    case AQUAPILOT_SHELLY_FILTER:
        return "filter";
    case AQUAPILOT_SHELLY_CO2:
        return "co2";
    case AQUAPILOT_SHELLY_AIR:
        return "air";
    default:
        return "unknown";
    }
}

static void print_plug_relay(aquapilot_shelly_plug_t plug)
{
    if (!aquapilot_settings_has_shelly_address(plug)) {
        printf("  %s: not configured\n", plug_label(plug));
        return;
    }

    shelly_plug_status_t status = {0};
    const esp_err_t err = shelly_client_get_plug_status(plug, &status);
    if (err != ESP_OK) {
        printf("  %s: read failed (%s)\n", plug_label(plug), esp_err_to_name(err));
        return;
    }

    if (!status.output_valid) {
        printf("  %s: relay state unknown\n", plug_label(plug));
        return;
    }

    printf("  %s: relay %s", plug_label(plug), status.output_on ? "ON" : "OFF");
    if (status.watts_valid) {
        printf(" (%u W)", (unsigned)status.watts);
    }
    printf("\n");
}

static int cmd_maint_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    bool nvs_enabled = false;
    (void)aquapilot_settings_get_maintenance_mode_enabled(&nvs_enabled);

    printf("active=%d running=%d nvs_enabled=%d\n", maintenance_mode_is_active() ? 1 : 0,
           maintenance_mode_sequence_running() ? 1 : 0, nvs_enabled ? 1 : 0);
    printf("status=%s\n", maintenance_mode_status_text());
    print_plug_relay(AQUAPILOT_SHELLY_FILTER);
    print_plug_relay(AQUAPILOT_SHELLY_CO2);
    print_plug_relay(AQUAPILOT_SHELLY_AIR);
    print_plug_relay(AQUAPILOT_SHELLY_HEATER);
    return 0;
}

void maintenance_console_register(void)
{
    static const esp_console_cmd_t cmds[] = {
        {
            .command = "maint_status",
            .help = "Print maintenance mode state and Shelly relay status",
            .func = &cmd_maint_status,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "register %s failed: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
