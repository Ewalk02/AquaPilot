#include "heater_console.h"

#include <stdio.h>
#include <stdlib.h>

#include "chihiros_ble.h"
#include "esp_console.h"
#include "esp_log.h"

static const char *TAG = "heater_console";

static int cmd_heater_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    chihiros_status_t st;
    if (!chihiros_ble_get_status(&st)) {
        printf("heater status unavailable\n");
        return 0;
    }

    printf("connected=%d subscribed=%d status_valid=%d stale=%d\n", st.connected, st.subscribed, st.status_valid,
           st.stale);
    if (st.status_valid) {
        printf("temp=%.1f F (%.1f C) watts=%u heating=%d\n", st.current_temp_f, st.current_temp_c,
               (unsigned)st.power_watts, st.heating);
        printf("status_a=0x%02x status_b=0x%02x last_status_ms=%lld\n", st.status_a, st.status_b,
               (long long)st.last_status_ms);
    }
    if (st.setpoint_f_last_sent > 0.0f) {
        printf("setpoint_last_sent=%.1f F\n", st.setpoint_f_last_sent);
    }
    printf("disconnect_count=%d\n", st.disconnect_count);
    return 0;
}

static int cmd_heater_set_f(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: heater_set_f <temp_F>\n");
        return 1;
    }

    float target = strtof(argv[1], NULL);
    esp_err_t err = chihiros_ble_set_target_f(target);
    if (err == ESP_OK) {
        printf("setpoint %.1f F sent\n", target);
        return 0;
    }
    printf("setpoint failed: %s\n", esp_err_to_name(err));
    return 1;
}

static int cmd_heater_reconnect(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    chihiros_ble_request_reconnect();
    printf("heater reconnect requested\n");
    return 0;
}

static int cmd_heater_refresh(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    chihiros_ble_request_status_refresh();
    printf("heater status refresh requested\n");
    return 0;
}

void heater_console_register(void)
{
    static const esp_console_cmd_t cmds[] = {
        {
            .command = "heater_status",
            .help = "Print Chihiros heater BLE status",
            .func = &cmd_heater_status,
        },
        {
            .command = "heater_set_f",
            .help = "Set heater target temperature in °F (60-86)",
            .func = &cmd_heater_set_f,
        },
        {
            .command = "heater_reconnect",
            .help = "Force heater BLE reconnect",
            .func = &cmd_heater_reconnect,
        },
        {
            .command = "heater_refresh",
            .help = "Re-send init sequence for status",
            .func = &cmd_heater_refresh,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "register %s failed: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
