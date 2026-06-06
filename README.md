# AquaPilot

Minimal ESP-IDF baseline for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** (7-inch, 1024×600). Boots to a dark dashboard with one **Tank Temperature** tile showing a mock value.

## Hardware

| Item | Value |
|------|-------|
| Board | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B |
| MCU | ESP32-P4 (32 MB NOR flash) |
| Display | 7-inch IPS, 1024×600, MIPI DSI |
| Touch | GT911 (initialized by BSP; not used in this milestone) |

## Toolchain

| Component | Version |
|-----------|---------|
| ESP-IDF | **5.5.x** (tested with 5.5.4) |
| Target | `esp32p4` |
| BSP | [`waveshare/esp32_p4_wifi6_touch_lcd_7b`](https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_7b) ^1.0.3 |
| Wi-Fi | [`espressif/esp_wifi_remote`](https://components.espressif.com/components/espressif/esp_wifi_remote) + [`espressif/esp_hosted`](https://components.espressif.com/components/espressif/esp_hosted) (C6 over SDIO) |
| UI | LVGL 9 (via `esp_lvgl_port`) |

## Prerequisites

1. Install [ESP-IDF 5.5.x](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/get-started/index.html).
2. Source the export script, for example:

```bash
source ~/esp/esp-idf/export.sh
# or
source ~/.espressif/v5.5.4/esp-idf/export.sh
```

3. Connect the board over USB (UART for serial monitor / flash).

## Build

```bash
cd /path/to/AquaPilot
idf.py set-target esp32p4
idf.py build
```

On first build, ESP-IDF Component Manager downloads the Waveshare BSP and its dependencies (`esp_lvgl_port`, `esp_lcd_ek79007`, `esp_lcd_touch_gt911`, LVGL).

## Flash

```bash
idf.py -p /dev/ttyACM0 flash
```

Replace `/dev/ttyACM0` with your serial port (`/dev/ttyUSB0`, etc.).

## Monitor

```bash
idf.py -p /dev/ttyACM0 monitor
```

Exit monitor: `Ctrl+]`.

Build, flash, and monitor in one step:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## Expected boot log

```
I (xxx) aquapilot: AquaPilot starting
I (xxx) aquapilot: display: 1024x600, draw buffer 51200 px, PSRAM=1
I (xxx) aquapilot: backlight on
I (xxx) aquapilot_ui: main dashboard ready
I (xxx) aquapilot: ready — main dashboard visible
```

If display init fails you should see:

```
E (xxx) aquapilot: bsp_display_start_with_config() failed
E (xxx) aquapilot: display init failed — halting (check wiring and BSP config)
```

## Expected screen

- Full-screen **dark dashboard** with a **3×3 grid** of tiles
- **Top-left:** **Tank Temperature** tile showing **77.0 °F** (mock)
- **Bottom-right:** **Settings** tile — tap to open Wi-Fi settings
- Other grid cells are empty placeholders for future tiles
- Wi-Fi screen: SSID/password fields, on-screen keyboard, **Connect** and **Back**

## Wi-Fi (ESP32-C6 coprocessor)

This board has no native Wi-Fi on the P4; connectivity uses **ESP-Hosted** over SDIO to the onboard **ESP32-C6**. Credentials are saved to NVS when you tap **Connect** on the Settings screen. If credentials were saved previously, the device auto-connects at boot.

Optional defaults via menuconfig: **AquaPilot Configuration → Default WiFi SSID/Password**.

## Project layout

```
main/
  app_main.c              Display/LVGL init, Wi-Fi init, calls UI
  ui/
    aquapilot_ui.c/h      UI entry point
    screen_main.c/h       3×3 dashboard grid
    screen_wifi.c/h       Wi-Fi credentials screen
    tile_common.c/h       Shared tile styling
    tile_temp.c/h         Temperature tile
    tile_settings.c/h     Settings tile (bottom-right)
    ui_nav.c/h            Screen navigation
  net/
    wifi_manager.c/h      STA connect + reconnect
  storage/
    wifi_creds_nvs.c/h    NVS credential storage
  sensors/
    temp_source.c/h       Mock temperature source
```

## Testing checklist

- [ ] `idf.py set-target esp32p4` succeeds
- [ ] `idf.py build` completes with no errors
- [ ] Flash completes without timeout
- [ ] Serial shows `AquaPilot starting` and `ready — main dashboard visible`
- [ ] Backlight turns on; screen is not blank
- [ ] **3×3 grid** visible with empty placeholder cells
- [ ] **Tank Temperature** in top-left shows **77.0 °F**
- [ ] **Settings** tile in bottom-right opens Wi-Fi screen
- [ ] Can enter SSID/password and tap **Connect**
- [ ] Status line updates (connecting / connected + IP)
- [ ] **Back** returns to dashboard
- [ ] Board runs for 5+ minutes with no reboot loop
- [ ] No task watchdog errors in serial log
- [ ] Touch is inactive (no crashes when tapping; touch not wired into UI yet)

## Out of scope (this milestone)

BLE, real sensors, OTA, heater/CO2 control, Shelly, and additional dashboard tiles beyond temperature + settings.
