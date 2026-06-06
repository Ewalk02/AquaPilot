#include "light_console.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_log.h"
#include "fluval_ble.h"
#include "fluval_light_protocol.h"

static const char *TAG = "light_console";

static const char *mode_text(fluval_mode_t mode)
{
    switch (mode) {
    case FLUVAL_MODE_MANUAL:
        return "Manual";
    case FLUVAL_MODE_AUTO:
        return "Auto";
    default:
        return "Unknown";
    }
}

static int cmd_fluval_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    fluval_status_t st;
    if (!fluval_ble_get_status(&st)) {
        printf("Light status unavailable\n");
        return 0;
    }

    const char *link = "OFFLINE";
    if (st.connected && st.subscribed && st.status_valid && !st.stale) {
        link = "OK";
    } else if (st.connected) {
        link = "CONNECTING";
    } else if (st.stale) {
        link = "STALE";
    }

    printf("Plant 4.0\n");
    if (st.status_valid) {
        printf("Mode: %s\n", mode_text(st.mode));
        printf("Output: %u%%\n", (unsigned)st.avg_output);
        printf("Pink: %u\n", (unsigned)st.pink);
        printf("Blue: %u\n", (unsigned)st.blue);
        printf("Cold White: %u\n", (unsigned)st.cold_white);
        printf("White: %u\n", (unsigned)st.white);
        printf("Warm White: %u\n", (unsigned)st.warm_white);
    } else {
        printf("Mode: --\n");
        printf("Output: --\n");
    }
    printf("Link: %s\n", link);
    return 0;
}

static int cmd_fluval_read(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fluval_ble_request_poll_window();
    fluval_ble_request_status();
    printf("Light status query requested\n");
    return 0;
}

static int cmd_fluval_manual(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_err_t err = fluval_ble_set_manual();
    printf(err == ESP_OK ? "Manual mode command sent\n" : "Manual mode command failed\n");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_fluval_auto(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_err_t err = fluval_ble_set_auto();
    printf(err == ESP_OK ? "Auto mode command sent\n" : "Auto mode command failed\n");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_fluval_setall(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: fluval_setall <0-100>\n");
        return 1;
    }

    const int percent = atoi(argv[1]);
    if (percent < 0 || percent > 100) {
        printf("Percent must be 0-100\n");
        return 1;
    }

    const esp_err_t err = fluval_ble_set_all((uint8_t)percent);
    printf(err == ESP_OK ? "Set all channels to %d%%\n" : "Set all failed\n", percent);
    return err == ESP_OK ? 0 : 1;
}

static int cmd_fluval_set(int argc, char **argv)
{
    if (argc < 6) {
        printf("Usage: fluval_set <pink> <blue> <cold_white> <white> <warm_white>\n");
        return 1;
    }

    const uint8_t pink = (uint8_t)atoi(argv[1]);
    const uint8_t blue = (uint8_t)atoi(argv[2]);
    const uint8_t cold_white = (uint8_t)atoi(argv[3]);
    const uint8_t white = (uint8_t)atoi(argv[4]);
    const uint8_t warm_white = (uint8_t)atoi(argv[5]);

    const esp_err_t err = fluval_ble_set_channels(pink, blue, cold_white, white, warm_white);
    printf(err == ESP_OK ? "Channel command sent\n" : "Channel command failed\n");
    return err == ESP_OK ? 0 : 1;
}

void light_console_register(void)
{
    const esp_console_cmd_t cmds[] = {
        {
            .command = "fluval_status",
            .help = "Print Fluval Plant 4.0 BLE status",
            .func = cmd_fluval_status,
        },
        {
            .command = "fluval_read",
            .help = "Request a light status read",
            .func = cmd_fluval_read,
        },
        {
            .command = "fluval_manual",
            .help = "Switch Fluval light to Manual mode",
            .func = cmd_fluval_manual,
        },
        {
            .command = "fluval_auto",
            .help = "Switch Fluval light to saved Auto mode",
            .func = cmd_fluval_auto,
        },
        {
            .command = "fluval_setall",
            .help = "Set all Fluval channels to one brightness percent",
            .func = cmd_fluval_setall,
        },
        {
            .command = "fluval_set",
            .help = "Set Fluval channel mix (pink blue cold white white warm_white)",
            .func = cmd_fluval_set,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "register %s failed: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
