#include "aquapilot_settings.h"

#include <stdint.h>
#include <string.h>

#include "aquapilot_nvs.h"
#include "chihiros_heater_protocol.h"
#include "esp_log.h"

static const char *TAG = "settings";
static const char *NVS_KEY = "settings_v1";
static const char *LEGACY_TEMP_KEY = "temp_range_v1";

#define SETTINGS_MAGIC_V1 0x41515031u /* AQP1 */
#define SETTINGS_MAGIC_V2 0x41515032u /* AQP2 */
#define SETTINGS_MAGIC_V3 0x41515033u /* AQP3 */
#define SETTINGS_MAGIC_V4 0x41515034u /* AQP4 */
#define SETTINGS_MAGIC_V5 0x41515035u /* AQP5 */
#define SETTINGS_MAGIC_V6 0x41515036u /* AQP6 */
#define SETTINGS_MAGIC_V7 0x41515037u /* AQP7 */
#define SETTINGS_MAGIC_V8 0x41515038u /* AQP8 */
#define SETTINGS_MAGIC_V9  0x41515039u /* AQP9 */
#define SETTINGS_MAGIC_V10 0x4151503Au /* AQP10 */
#define SETTINGS_MAGIC_V11 0x4151503Bu /* AQP11 */
#define SETTINGS_MAGIC_V12 0x4151503Cu /* AQP12 */
#define SETTINGS_MAGIC_V13 0x4151503Du /* AQP13 */
#define SETTINGS_MAGIC_V14 0x4151503Eu /* AQP14 */
#define SETTINGS_MAGIC_V15 0x4151503Fu /* AQP15 */

#define DEFAULT_DISPLAY_BRIGHTNESS_PCT 100
#define DISPLAY_BRIGHTNESS_MIN         5
#define DISPLAY_BRIGHTNESS_MAX         100

#define DEFAULT_FEEDER_START_H          8
#define DEFAULT_FEEDER_START_M          0
#define DEFAULT_FEEDER_END_H            20
#define DEFAULT_FEEDER_END_M            0
#define DEFAULT_FEEDER_TIMES_PER_DAY    2
#define DEFAULT_FEEDER_AMOUNT_SECONDS   3
#define FEEDER_TIMES_PER_DAY_MIN        1
#define FEEDER_TIMES_PER_DAY_MAX        12
#define FEEDER_AMOUNT_SECONDS_MIN       1
#define FEEDER_AMOUNT_SECONDS_MAX       120

#define DEFAULT_FILTER_BAND_GREEN_PCT   10
#define DEFAULT_FILTER_BAND_YELLOW_PCT  25
#define DEFAULT_FILTER_BAND_RED_PCT     40
#define DEFAULT_FILTER_BAND_RED_CUTOFF_PCT 50

#define DEFAULT_SETPOINT_F      77.0f
#define DEFAULT_DELTA_PLUS_F    5.0f
#define DEFAULT_DELTA_MINUS_F   5.0f
#define DEFAULT_CO2_ON_H        7
#define DEFAULT_CO2_ON_M        0
#define DEFAULT_CO2_OFF_H       21
#define DEFAULT_CO2_OFF_M       0
#define MIN_DELTA_F             0.1f
#define MAX_DELTA_F             25.0f
#define DEFAULT_TIMEZONE        "UTC0"
#define SHELLY_ADDR_LEN         64

typedef struct __attribute__((packed)) {
    uint32_t magic;
    float temp_min_f;
    float temp_max_f;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t reserved;
} settings_blob_v1_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    float temp_min_f;
    float temp_max_f;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
} settings_blob_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
} settings_blob_v3_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
} settings_blob_v4_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t reserved;
} settings_blob_v5_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
} settings_blob_v6_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
} settings_blob_v7_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
} settings_blob_v8_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
} settings_blob_v9_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
    uint8_t filter_band_green_pct;
    uint8_t filter_band_yellow_pct;
    uint8_t filter_band_red_pct;
} settings_blob_v10_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
    uint8_t filter_band_green_pct;
    uint8_t filter_band_yellow_pct;
    uint8_t filter_band_red_pct;
    uint8_t filter_band_red_cutoff_pct;
} settings_blob_v11_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
    uint8_t filter_band_green_pct;
    uint8_t filter_band_yellow_pct;
    uint8_t filter_band_red_pct;
    uint8_t filter_band_red_cutoff_pct;
    uint8_t maintenance_mode_enabled;
} settings_blob_v12_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
    uint8_t filter_band_green_pct;
    uint8_t filter_band_yellow_pct;
    uint8_t filter_band_red_pct;
    uint8_t filter_band_red_cutoff_pct;
    uint8_t maintenance_mode_enabled;
    uint8_t feeder_enabled;
    uint8_t feeder_start_h;
    uint8_t feeder_start_m;
    uint8_t feeder_end_h;
    uint8_t feeder_end_m;
    uint8_t feeder_times_per_day;
    uint16_t feeder_amount_seconds;
} settings_blob_v13_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
    uint8_t filter_band_green_pct;
    uint8_t filter_band_yellow_pct;
    uint8_t filter_band_red_pct;
    uint8_t filter_band_red_cutoff_pct;
    uint8_t maintenance_mode_enabled;
    uint8_t feeder_enabled;
    uint8_t feeder_start_h;
    uint8_t feeder_start_m;
    uint8_t feeder_end_h;
    uint8_t feeder_end_m;
    uint8_t feeder_times_per_day;
    uint16_t feeder_amount_seconds;
    char feeder_host[SHELLY_ADDR_LEN];
} settings_blob_v14_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t co2_on_h;
    uint8_t co2_on_m;
    uint8_t co2_off_h;
    uint8_t co2_off_m;
    uint8_t filter_calibrated;
    uint8_t heater_setpoint_valid;
    float heater_setpoint_f;
    float temp_delta_plus_f;
    float temp_delta_minus_f;
    char shelly_heater[SHELLY_ADDR_LEN];
    char shelly_filter[SHELLY_ADDR_LEN];
    char shelly_co2[SHELLY_ADDR_LEN];
    uint8_t heater_override_enabled;
    uint8_t wifi_time_enabled;
    uint8_t manual_time_valid;
    uint8_t reserved;
    int64_t manual_epoch;
    char timezone[AQUAPILOT_TIMEZONE_MAX];
    uint8_t co2_power_monitor_enabled;
    uint8_t heater_shelly_power_monitor_enabled;
    float filter_baseline_watts;
    uint8_t filter_band_green_pct;
    uint8_t filter_band_yellow_pct;
    uint8_t filter_band_red_pct;
    uint8_t filter_band_red_cutoff_pct;
    uint8_t maintenance_mode_enabled;
    uint8_t feeder_enabled;
    uint8_t feeder_start_h;
    uint8_t feeder_start_m;
    uint8_t feeder_end_h;
    uint8_t feeder_end_m;
    uint8_t feeder_times_per_day;
    uint16_t feeder_amount_seconds;
    char feeder_host[SHELLY_ADDR_LEN];
    uint8_t display_flip_180;
    uint8_t display_brightness_pct;
} settings_blob_t;

typedef struct __attribute__((packed)) {
    float min_f;
    float max_f;
    uint8_t stored;
    uint8_t pad[3];
} legacy_temp_blob_t;

static settings_blob_t s_settings;

static void settings_defaults(settings_blob_t *s)
{
    memset(s, 0, sizeof(*s));
    s->magic = SETTINGS_MAGIC_V15;
    s->co2_on_h = DEFAULT_CO2_ON_H;
    s->co2_on_m = DEFAULT_CO2_ON_M;
    s->co2_off_h = DEFAULT_CO2_OFF_H;
    s->co2_off_m = DEFAULT_CO2_OFF_M;
    s->filter_calibrated = 0;
    s->heater_setpoint_valid = 1;
    s->heater_setpoint_f = DEFAULT_SETPOINT_F;
    s->temp_delta_plus_f = DEFAULT_DELTA_PLUS_F;
    s->temp_delta_minus_f = DEFAULT_DELTA_MINUS_F;
    s->shelly_heater[0] = '\0';
    s->shelly_filter[0] = '\0';
    s->shelly_co2[0] = '\0';
    s->feeder_host[0] = '\0';
    s->heater_override_enabled = 0;
    s->co2_power_monitor_enabled = 0;
    s->heater_shelly_power_monitor_enabled = 0;
    s->filter_baseline_watts = 0.0f;
    s->filter_band_green_pct = DEFAULT_FILTER_BAND_GREEN_PCT;
    s->filter_band_yellow_pct = DEFAULT_FILTER_BAND_YELLOW_PCT;
    s->filter_band_red_pct = DEFAULT_FILTER_BAND_RED_PCT;
    s->filter_band_red_cutoff_pct = DEFAULT_FILTER_BAND_RED_CUTOFF_PCT;
    s->maintenance_mode_enabled = 0;
    s->feeder_enabled = 0;
    s->feeder_start_h = DEFAULT_FEEDER_START_H;
    s->feeder_start_m = DEFAULT_FEEDER_START_M;
    s->feeder_end_h = DEFAULT_FEEDER_END_H;
    s->feeder_end_m = DEFAULT_FEEDER_END_M;
    s->feeder_times_per_day = DEFAULT_FEEDER_TIMES_PER_DAY;
    s->feeder_amount_seconds = DEFAULT_FEEDER_AMOUNT_SECONDS;
    s->display_flip_180 = 0;
    s->display_brightness_pct = DEFAULT_DISPLAY_BRIGHTNESS_PCT;
    s->wifi_time_enabled = 1;
    s->manual_time_valid = 0;
    s->reserved = 0;
    s->manual_epoch = 0;
    strncpy(s->timezone, DEFAULT_TIMEZONE, AQUAPILOT_TIMEZONE_MAX - 1);
    s->timezone[AQUAPILOT_TIMEZONE_MAX - 1] = '\0';
}

static void ensure_time_strings_null_terminated(void)
{
    s_settings.timezone[AQUAPILOT_TIMEZONE_MAX - 1] = '\0';
}

static void apply_filter_band_defaults(void)
{
    s_settings.filter_band_green_pct = DEFAULT_FILTER_BAND_GREEN_PCT;
    s_settings.filter_band_yellow_pct = DEFAULT_FILTER_BAND_YELLOW_PCT;
    s_settings.filter_band_red_pct = DEFAULT_FILTER_BAND_RED_PCT;
    s_settings.filter_band_red_cutoff_pct = DEFAULT_FILTER_BAND_RED_CUTOFF_PCT;
}

static bool filter_bands_valid(uint8_t green_pct, uint8_t yellow_pct, uint8_t red_pct, uint8_t red_cutoff_pct)
{
    return green_pct >= 1 && yellow_pct > green_pct && red_pct > yellow_pct && red_cutoff_pct > red_pct &&
           red_cutoff_pct <= 100;
}

static void ensure_shelly_strings_null_terminated(void)
{
    s_settings.shelly_heater[SHELLY_ADDR_LEN - 1] = '\0';
    s_settings.shelly_filter[SHELLY_ADDR_LEN - 1] = '\0';
    s_settings.shelly_co2[SHELLY_ADDR_LEN - 1] = '\0';
    s_settings.feeder_host[SHELLY_ADDR_LEN - 1] = '\0';
    ensure_time_strings_null_terminated();
}

static bool timezone_valid(const char *tz)
{
    if (tz == NULL || tz[0] == '\0') {
        return false;
    }

    size_t len = strlen(tz);
    if (len >= AQUAPILOT_TIMEZONE_MAX) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        const char c = tz[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '+' || c == ',' || c == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static char *shelly_slot(settings_blob_t *s, aquapilot_shelly_plug_t plug)
{
    switch (plug) {
    case AQUAPILOT_SHELLY_HEATER:
        return s->shelly_heater;
    case AQUAPILOT_SHELLY_FILTER:
        return s->shelly_filter;
    case AQUAPILOT_SHELLY_CO2:
        return s->shelly_co2;
    default:
        return NULL;
    }
}

static bool shelly_char_allowed(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
           c == ':';
}

static void normalize_shelly_address(const char *in, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    out[0] = '\0';
    if (in == NULL) {
        return;
    }

    while (*in == ' ' || *in == '\t') {
        in++;
    }

    if (strncmp(in, "http://", 7) == 0) {
        in += 7;
    } else if (strncmp(in, "https://", 8) == 0) {
        in += 8;
    }

    size_t n = 0;
    while (in[n] != '\0' && n < out_len - 1) {
        out[n] = in[n];
        n++;
    }
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == ' ' || out[n - 1] == '\t')) {
        n--;
    }
    out[n] = '\0';

    if (n > 0 && n <= 3) {
        bool digits_only = true;
        for (size_t i = 0; i < n; i++) {
            if (out[i] < '0' || out[i] > '9') {
                digits_only = false;
                break;
            }
        }
        if (digits_only) {
            char expanded[SHELLY_ADDR_LEN];
            snprintf(expanded, sizeof(expanded), "192.168.1.%s", out);
            strncpy(out, expanded, out_len - 1);
            out[out_len - 1] = '\0';
        }
    }
}

static bool shelly_address_valid(const char *address)
{
    if (address == NULL) {
        return false;
    }

    if (address[0] == '\0') {
        return true;
    }

    size_t len = strlen(address);
    if (len >= SHELLY_ADDR_LEN) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (!shelly_char_allowed(address[i])) {
            return false;
        }
    }
    return true;
}

static bool save_settings(void)
{
    esp_err_t err = aquapilot_nvs_set_blob(NVS_KEY, &s_settings, sizeof(s_settings));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static float effective_setpoint_f(void)
{
    if (s_settings.heater_setpoint_valid) {
        return s_settings.heater_setpoint_f;
    }
    return DEFAULT_SETPOINT_F;
}

static void compute_temp_range(float *min_f, float *max_f)
{
    const float sp = effective_setpoint_f();
    *min_f = sp - s_settings.temp_delta_minus_f;
    *max_f = sp + s_settings.temp_delta_plus_f;
}

static void set_deltas_from_range(float min_f, float max_f, float setpoint_f)
{
    float delta_minus = setpoint_f - min_f;
    float delta_plus = max_f - setpoint_f;
    if (delta_minus < MIN_DELTA_F) {
        delta_minus = MIN_DELTA_F;
    }
    if (delta_plus < MIN_DELTA_F) {
        delta_plus = MIN_DELTA_F;
    }
    s_settings.temp_delta_minus_f = delta_minus;
    s_settings.temp_delta_plus_f = delta_plus;
}

static void upgrade_v2_blob(const settings_blob_v2_t *loaded)
{
    s_settings.co2_on_h = loaded->co2_on_h;
    s_settings.co2_on_m = loaded->co2_on_m;
    s_settings.co2_off_h = loaded->co2_off_h;
    s_settings.co2_off_m = loaded->co2_off_m;
    s_settings.filter_calibrated = loaded->filter_calibrated;
    s_settings.heater_setpoint_valid = loaded->heater_setpoint_valid;
    s_settings.heater_setpoint_f = loaded->heater_setpoint_valid ? loaded->heater_setpoint_f : DEFAULT_SETPOINT_F;

    float min_f = loaded->temp_min_f;
    float max_f = loaded->temp_max_f;
    if (min_f > max_f) {
        float tmp = min_f;
        min_f = max_f;
        max_f = tmp;
    }

    float setpoint_f = s_settings.heater_setpoint_f;
    if (!loaded->heater_setpoint_valid) {
        setpoint_f = (min_f + max_f) * 0.5f;
        s_settings.heater_setpoint_f = setpoint_f;
        s_settings.heater_setpoint_valid = 1;
    }

    set_deltas_from_range(min_f, max_f, setpoint_f);
    s_settings.co2_power_monitor_enabled = 0;
    s_settings.heater_shelly_power_monitor_enabled = 0;
    s_settings.filter_baseline_watts = 0.0f;
    s_settings.filter_calibrated = 0;
    apply_filter_band_defaults();
    s_settings.magic = SETTINGS_MAGIC_V12;
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v2 → v10 (setpoint %.1f F, Δ- %.1f, Δ+ %.1f)", setpoint_f,
             s_settings.temp_delta_minus_f, s_settings.temp_delta_plus_f);
}

static void upgrade_v3_blob(const settings_blob_v3_t *loaded)
{
    s_settings.co2_on_h = loaded->co2_on_h;
    s_settings.co2_on_m = loaded->co2_on_m;
    s_settings.co2_off_h = loaded->co2_off_h;
    s_settings.co2_off_m = loaded->co2_off_m;
    s_settings.filter_calibrated = loaded->filter_calibrated;
    s_settings.heater_setpoint_valid = loaded->heater_setpoint_valid;
    s_settings.heater_setpoint_f = loaded->heater_setpoint_valid ? loaded->heater_setpoint_f : DEFAULT_SETPOINT_F;
    s_settings.temp_delta_plus_f = loaded->temp_delta_plus_f;
    s_settings.temp_delta_minus_f = loaded->temp_delta_minus_f;
    s_settings.shelly_heater[0] = '\0';
    s_settings.shelly_filter[0] = '\0';
    s_settings.shelly_co2[0] = '\0';
    s_settings.heater_override_enabled = 0;
    s_settings.co2_power_monitor_enabled = 0;
    s_settings.heater_shelly_power_monitor_enabled = 0;
    s_settings.filter_baseline_watts = 0.0f;
    s_settings.filter_calibrated = 0;
    s_settings.reserved = 0;
    apply_filter_band_defaults();
    s_settings.magic = SETTINGS_MAGIC_V12;
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v3 → v10");
}

static void upgrade_v5_blob(const settings_blob_v5_t *loaded)
{
    s_settings.co2_on_h = loaded->co2_on_h;
    s_settings.co2_on_m = loaded->co2_on_m;
    s_settings.co2_off_h = loaded->co2_off_h;
    s_settings.co2_off_m = loaded->co2_off_m;
    s_settings.filter_calibrated = loaded->filter_calibrated;
    s_settings.heater_setpoint_valid = loaded->heater_setpoint_valid;
    s_settings.heater_setpoint_f = loaded->heater_setpoint_valid ? loaded->heater_setpoint_f : DEFAULT_SETPOINT_F;
    s_settings.temp_delta_plus_f = loaded->temp_delta_plus_f;
    s_settings.temp_delta_minus_f = loaded->temp_delta_minus_f;
    memcpy(s_settings.shelly_heater, loaded->shelly_heater, SHELLY_ADDR_LEN);
    memcpy(s_settings.shelly_filter, loaded->shelly_filter, SHELLY_ADDR_LEN);
    memcpy(s_settings.shelly_co2, loaded->shelly_co2, SHELLY_ADDR_LEN);
    s_settings.heater_override_enabled = loaded->heater_override_enabled;
    s_settings.wifi_time_enabled = 1;
    s_settings.manual_time_valid = 0;
    s_settings.manual_epoch = 0;
    s_settings.reserved = 0;
    strncpy(s_settings.timezone, DEFAULT_TIMEZONE, AQUAPILOT_TIMEZONE_MAX - 1);
    s_settings.timezone[AQUAPILOT_TIMEZONE_MAX - 1] = '\0';
    s_settings.co2_power_monitor_enabled = 0;
    s_settings.heater_shelly_power_monitor_enabled = 0;
    s_settings.filter_baseline_watts = 0.0f;
    s_settings.filter_calibrated = 0;
    apply_filter_band_defaults();
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v5 → v10");
}

static void upgrade_v4_blob(const settings_blob_v4_t *loaded)
{
    s_settings.co2_on_h = loaded->co2_on_h;
    s_settings.co2_on_m = loaded->co2_on_m;
    s_settings.co2_off_h = loaded->co2_off_h;
    s_settings.co2_off_m = loaded->co2_off_m;
    s_settings.filter_calibrated = loaded->filter_calibrated;
    s_settings.heater_setpoint_valid = loaded->heater_setpoint_valid;
    s_settings.heater_setpoint_f = loaded->heater_setpoint_valid ? loaded->heater_setpoint_f : DEFAULT_SETPOINT_F;
    s_settings.temp_delta_plus_f = loaded->temp_delta_plus_f;
    s_settings.temp_delta_minus_f = loaded->temp_delta_minus_f;
    memcpy(s_settings.shelly_heater, loaded->shelly_heater, SHELLY_ADDR_LEN);
    memcpy(s_settings.shelly_filter, loaded->shelly_filter, SHELLY_ADDR_LEN);
    memcpy(s_settings.shelly_co2, loaded->shelly_co2, SHELLY_ADDR_LEN);
    s_settings.heater_override_enabled = 0;
    s_settings.co2_power_monitor_enabled = 0;
    s_settings.heater_shelly_power_monitor_enabled = 0;
    s_settings.filter_baseline_watts = 0.0f;
    s_settings.filter_calibrated = 0;
    s_settings.reserved = 0;
    apply_filter_band_defaults();
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v4 → v10");
}

static void upgrade_v6_blob(const settings_blob_v6_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v6_t));
    s_settings.co2_power_monitor_enabled = 0;
    s_settings.heater_shelly_power_monitor_enabled = 0;
    s_settings.filter_baseline_watts = 0.0f;
    s_settings.filter_calibrated = 0;
    apply_filter_band_defaults();
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v6 → v10");
}

static void upgrade_v7_blob(const settings_blob_v7_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v7_t));
    s_settings.heater_shelly_power_monitor_enabled = 0;
    s_settings.filter_baseline_watts = 0.0f;
    apply_filter_band_defaults();
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v7 → v10");
}

static void upgrade_v10_blob(const settings_blob_v10_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v10_t));
    s_settings.filter_band_red_cutoff_pct = DEFAULT_FILTER_BAND_RED_CUTOFF_PCT;
    s_settings.maintenance_mode_enabled = 0;
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v10 → v12");
}

static void upgrade_v11_blob(const settings_blob_v11_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v11_t));
    s_settings.maintenance_mode_enabled = 0;
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v11 → v12");
}

static void apply_feeder_defaults(void)
{
    s_settings.feeder_enabled = 0;
    s_settings.feeder_start_h = DEFAULT_FEEDER_START_H;
    s_settings.feeder_start_m = DEFAULT_FEEDER_START_M;
    s_settings.feeder_end_h = DEFAULT_FEEDER_END_H;
    s_settings.feeder_end_m = DEFAULT_FEEDER_END_M;
    s_settings.feeder_times_per_day = DEFAULT_FEEDER_TIMES_PER_DAY;
    s_settings.feeder_amount_seconds = DEFAULT_FEEDER_AMOUNT_SECONDS;
}

static void upgrade_v12_blob(const settings_blob_v12_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v12_t));
    apply_feeder_defaults();
    s_settings.magic = SETTINGS_MAGIC_V13;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v12 → v13");
}

static void upgrade_v13_blob(const settings_blob_v13_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v13_t));
    s_settings.feeder_host[0] = '\0';
    s_settings.display_flip_180 = 0;
    s_settings.display_brightness_pct = DEFAULT_DISPLAY_BRIGHTNESS_PCT;
    s_settings.magic = SETTINGS_MAGIC_V15;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v13 → v15");
}

static void upgrade_v14_blob(const settings_blob_v14_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v14_t));
    s_settings.display_flip_180 = 0;
    s_settings.display_brightness_pct = DEFAULT_DISPLAY_BRIGHTNESS_PCT;
    s_settings.magic = SETTINGS_MAGIC_V15;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v14 → v15");
}

static void upgrade_v9_blob(const settings_blob_v9_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v9_t));
    s_settings.filter_band_green_pct = DEFAULT_FILTER_BAND_GREEN_PCT;
    s_settings.filter_band_yellow_pct = DEFAULT_FILTER_BAND_YELLOW_PCT;
    s_settings.filter_band_red_pct = DEFAULT_FILTER_BAND_RED_PCT;
    s_settings.filter_band_red_cutoff_pct = DEFAULT_FILTER_BAND_RED_CUTOFF_PCT;
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v9 → v11");
}

static void upgrade_v8_blob(const settings_blob_v8_t *loaded)
{
    memset(&s_settings, 0, sizeof(s_settings));
    memcpy(&s_settings, loaded, sizeof(settings_blob_v8_t));
    s_settings.filter_baseline_watts = 0.0f;
    apply_filter_band_defaults();
    if (s_settings.filter_calibrated != 0) {
        s_settings.filter_calibrated = 0;
        ESP_LOGI(TAG, "cleared legacy filter calibration (no baseline stored)");
    }
    s_settings.filter_band_green_pct = DEFAULT_FILTER_BAND_GREEN_PCT;
    s_settings.filter_band_yellow_pct = DEFAULT_FILTER_BAND_YELLOW_PCT;
    s_settings.filter_band_red_pct = DEFAULT_FILTER_BAND_RED_PCT;
    s_settings.filter_band_red_cutoff_pct = DEFAULT_FILTER_BAND_RED_CUTOFF_PCT;
    s_settings.magic = SETTINGS_MAGIC_V12;
    ensure_shelly_strings_null_terminated();
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v8 → v11");
}

static void migrate_legacy_temp_range(void)
{
    legacy_temp_blob_t legacy = {0};
    size_t size = sizeof(legacy);
    if (aquapilot_nvs_get_blob(LEGACY_TEMP_KEY, &legacy, &size) != ESP_OK) {
        return;
    }
    if (size != sizeof(legacy) || !legacy.stored) {
        return;
    }

    settings_defaults(&s_settings);
    set_deltas_from_range(legacy.min_f, legacy.max_f, DEFAULT_SETPOINT_F);
    save_settings();
    ESP_LOGI(TAG, "migrated legacy temp range to deltas (Δ- %.1f, Δ+ %.1f)", s_settings.temp_delta_minus_f,
             s_settings.temp_delta_plus_f);
}

static void load_v1_blob(const settings_blob_v1_t *loaded)
{
    settings_defaults(&s_settings);
    s_settings.co2_on_h = loaded->co2_on_h;
    s_settings.co2_on_m = loaded->co2_on_m;
    s_settings.co2_off_h = loaded->co2_off_h;
    s_settings.co2_off_m = loaded->co2_off_m;
    s_settings.filter_calibrated = loaded->filter_calibrated;

    float min_f = loaded->temp_min_f;
    float max_f = loaded->temp_max_f;
    if (min_f > max_f) {
        float tmp = min_f;
        min_f = max_f;
        max_f = tmp;
    }
    set_deltas_from_range(min_f, max_f, DEFAULT_SETPOINT_F);
    save_settings();
    ESP_LOGI(TAG, "upgraded settings v1 → v6");
}

void aquapilot_settings_init(void)
{
    settings_defaults(&s_settings);

    settings_blob_t loaded = {0};
    size_t size = sizeof(loaded);
    esp_err_t err = aquapilot_nvs_get_blob(NVS_KEY, &loaded, &size);
    if (err == ESP_OK && size == sizeof(loaded) && loaded.magic == SETTINGS_MAGIC_V15) {
        s_settings = loaded;
        ensure_shelly_strings_null_terminated();
        float min_f = 0.0f;
        float max_f = 0.0f;
        compute_temp_range(&min_f, &max_f);
        ESP_LOGI(TAG, "loaded settings (setpoint %.1f F, range %.1f–%.1f F, tz %s)", effective_setpoint_f(), min_f,
                 max_f, s_settings.timezone);
        return;
    }

    settings_blob_v14_t loaded_v14 = {0};
    size = sizeof(loaded_v14);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v14, &size);
    if (err == ESP_OK && size == sizeof(loaded_v14) && loaded_v14.magic == SETTINGS_MAGIC_V14) {
        settings_defaults(&s_settings);
        upgrade_v14_blob(&loaded_v14);
        return;
    }

    settings_blob_v13_t loaded_v13 = {0};
    size = sizeof(loaded_v13);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v13, &size);
    if (err == ESP_OK && size == sizeof(loaded_v13) && loaded_v13.magic == SETTINGS_MAGIC_V13) {
        settings_defaults(&s_settings);
        upgrade_v13_blob(&loaded_v13);
        return;
    }

    settings_blob_v12_t loaded_v12 = {0};
    size = sizeof(loaded_v12);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v12, &size);
    if (err == ESP_OK && size == sizeof(loaded_v12) && loaded_v12.magic == SETTINGS_MAGIC_V12) {
        settings_defaults(&s_settings);
        upgrade_v12_blob(&loaded_v12);
        return;
    }

    settings_blob_v11_t loaded_v11 = {0};
    size = sizeof(loaded_v11);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v11, &size);
    if (err == ESP_OK && size == sizeof(loaded_v11) && loaded_v11.magic == SETTINGS_MAGIC_V11) {
        settings_defaults(&s_settings);
        upgrade_v11_blob(&loaded_v11);
        return;
    }

    settings_blob_v10_t loaded_v10 = {0};
    size = sizeof(loaded_v10);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v10, &size);
    if (err == ESP_OK && size == sizeof(loaded_v10) && loaded_v10.magic == SETTINGS_MAGIC_V10) {
        settings_defaults(&s_settings);
        upgrade_v10_blob(&loaded_v10);
        return;
    }

    settings_blob_v9_t loaded_v9 = {0};
    size = sizeof(loaded_v9);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v9, &size);
    if (err == ESP_OK && size == sizeof(loaded_v9) && loaded_v9.magic == SETTINGS_MAGIC_V9) {
        settings_defaults(&s_settings);
        upgrade_v9_blob(&loaded_v9);
        return;
    }

    settings_blob_v8_t loaded_v8 = {0};
    size = sizeof(loaded_v8);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v8, &size);
    if (err == ESP_OK && size == sizeof(loaded_v8) && loaded_v8.magic == SETTINGS_MAGIC_V8) {
        settings_defaults(&s_settings);
        upgrade_v8_blob(&loaded_v8);
        return;
    }

    settings_blob_v7_t loaded_v7 = {0};
    size = sizeof(loaded_v7);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v7, &size);
    if (err == ESP_OK && size == sizeof(loaded_v7) && loaded_v7.magic == SETTINGS_MAGIC_V7) {
        settings_defaults(&s_settings);
        upgrade_v7_blob(&loaded_v7);
        return;
    }

    settings_blob_v6_t loaded_v6 = {0};
    size = sizeof(loaded_v6);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v6, &size);
    if (err == ESP_OK && size == sizeof(loaded_v6) && loaded_v6.magic == SETTINGS_MAGIC_V6) {
        settings_defaults(&s_settings);
        upgrade_v6_blob(&loaded_v6);
        return;
    }

    settings_blob_v5_t loaded_v5 = {0};
    size = sizeof(loaded_v5);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v5, &size);
    if (err == ESP_OK && size == sizeof(loaded_v5) && loaded_v5.magic == SETTINGS_MAGIC_V5) {
        settings_defaults(&s_settings);
        upgrade_v5_blob(&loaded_v5);
        return;
    }

    settings_blob_v4_t loaded_v4 = {0};
    size = sizeof(loaded_v4);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v4, &size);
    if (err == ESP_OK && size == sizeof(loaded_v4) && loaded_v4.magic == SETTINGS_MAGIC_V4) {
        settings_defaults(&s_settings);
        upgrade_v4_blob(&loaded_v4);
        return;
    }

    settings_blob_v3_t loaded_v3 = {0};
    size = sizeof(loaded_v3);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v3, &size);
    if (err == ESP_OK && size == sizeof(loaded_v3) && loaded_v3.magic == SETTINGS_MAGIC_V3) {
        settings_defaults(&s_settings);
        upgrade_v3_blob(&loaded_v3);
        return;
    }

    settings_blob_v2_t loaded_v2 = {0};
    size = sizeof(loaded_v2);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v2, &size);
    if (err == ESP_OK && size == sizeof(loaded_v2) && loaded_v2.magic == SETTINGS_MAGIC_V2) {
        settings_defaults(&s_settings);
        upgrade_v2_blob(&loaded_v2);
        return;
    }

    settings_blob_v1_t loaded_v1 = {0};
    size = sizeof(loaded_v1);
    err = aquapilot_nvs_get_blob(NVS_KEY, &loaded_v1, &size);
    if (err == ESP_OK && size == sizeof(loaded_v1) && loaded_v1.magic == SETTINGS_MAGIC_V1) {
        load_v1_blob(&loaded_v1);
        return;
    }

    migrate_legacy_temp_range();
}

bool aquapilot_settings_get_temp_range(float *min_f, float *max_f)
{
    if (min_f == NULL || max_f == NULL) {
        return false;
    }
    compute_temp_range(min_f, max_f);
    return true;
}

bool aquapilot_settings_temp_contains(float temp_f)
{
    float min_f = 0.0f;
    float max_f = 0.0f;
    compute_temp_range(&min_f, &max_f);
    return temp_f >= min_f && temp_f <= max_f;
}

bool aquapilot_settings_get_temp_deltas(float *delta_plus_f, float *delta_minus_f)
{
    if (delta_plus_f == NULL || delta_minus_f == NULL) {
        return false;
    }
    *delta_plus_f = s_settings.temp_delta_plus_f;
    *delta_minus_f = s_settings.temp_delta_minus_f;
    return true;
}

static bool delta_valid(float delta_f)
{
    return delta_f >= MIN_DELTA_F && delta_f <= MAX_DELTA_F;
}

bool aquapilot_settings_set_temp_deltas(float delta_plus_f, float delta_minus_f)
{
    if (!delta_valid(delta_plus_f) || !delta_valid(delta_minus_f)) {
        return false;
    }
    s_settings.temp_delta_plus_f = delta_plus_f;
    s_settings.temp_delta_minus_f = delta_minus_f;
    ESP_LOGI(TAG, "temp deltas -%.1f / +%.1f F", delta_minus_f, delta_plus_f);
    return save_settings();
}

bool aquapilot_settings_get_co2_schedule(uint8_t *on_h, uint8_t *on_m, uint8_t *off_h, uint8_t *off_m)
{
    if (on_h == NULL || on_m == NULL || off_h == NULL || off_m == NULL) {
        return false;
    }
    *on_h = s_settings.co2_on_h;
    *on_m = s_settings.co2_on_m;
    *off_h = s_settings.co2_off_h;
    *off_m = s_settings.co2_off_m;
    return true;
}

bool aquapilot_settings_set_co2_schedule(uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m)
{
    if (on_h > 23 || off_h > 23 || on_m > 59 || off_m > 59) {
        return false;
    }
    s_settings.co2_on_h = on_h;
    s_settings.co2_on_m = on_m;
    s_settings.co2_off_h = off_h;
    s_settings.co2_off_m = off_m;
    ESP_LOGI(TAG, "CO2 schedule %02u:%02u – %02u:%02u", on_h, on_m, off_h, off_m);
    return save_settings();
}

bool aquapilot_settings_get_filter_calibrated(bool *calibrated)
{
    if (calibrated == NULL) {
        return false;
    }
    *calibrated = s_settings.filter_calibrated != 0;
    return true;
}

bool aquapilot_settings_set_filter_calibrated(bool calibrated)
{
    s_settings.filter_calibrated = calibrated ? 1 : 0;
    if (!calibrated) {
        s_settings.filter_baseline_watts = 0.0f;
    }
    ESP_LOGI(TAG, "filter calibrated=%d", calibrated);
    return save_settings();
}

bool aquapilot_settings_get_filter_baseline_watts(float *watts)
{
    if (watts == NULL || s_settings.filter_calibrated == 0 || s_settings.filter_baseline_watts <= 0.0f) {
        return false;
    }

    *watts = s_settings.filter_baseline_watts;
    return true;
}

bool aquapilot_settings_set_filter_baseline_watts(float watts)
{
    if (watts <= 0.0f) {
        return false;
    }

    s_settings.filter_baseline_watts = watts;
    s_settings.filter_calibrated = 1;
    ESP_LOGI(TAG, "filter baseline %.1f W", watts);
    return save_settings();
}

bool aquapilot_settings_get_filter_bands(uint8_t *green_pct, uint8_t *yellow_pct, uint8_t *red_pct,
                                         uint8_t *red_cutoff_pct)
{
    if (green_pct != NULL) {
        *green_pct = s_settings.filter_band_green_pct;
    }
    if (yellow_pct != NULL) {
        *yellow_pct = s_settings.filter_band_yellow_pct;
    }
    if (red_pct != NULL) {
        *red_pct = s_settings.filter_band_red_pct;
    }
    if (red_cutoff_pct != NULL) {
        *red_cutoff_pct = s_settings.filter_band_red_cutoff_pct;
    }
    return true;
}

bool aquapilot_settings_set_filter_bands(uint8_t green_pct, uint8_t yellow_pct, uint8_t red_pct,
                                         uint8_t red_cutoff_pct)
{
    if (!filter_bands_valid(green_pct, yellow_pct, red_pct, red_cutoff_pct)) {
        return false;
    }

    s_settings.filter_band_green_pct = green_pct;
    s_settings.filter_band_yellow_pct = yellow_pct;
    s_settings.filter_band_red_pct = red_pct;
    s_settings.filter_band_red_cutoff_pct = red_cutoff_pct;
    ESP_LOGI(TAG, "filter bands green±%u%% yellow±%u%% red±%u%% cutoff±%u%%", green_pct, yellow_pct, red_pct,
             red_cutoff_pct);
    return save_settings();
}

bool aquapilot_settings_has_heater_setpoint(void)
{
    return s_settings.heater_setpoint_valid != 0;
}

bool aquapilot_settings_get_heater_setpoint(float *setpoint_f)
{
    if (setpoint_f == NULL || !s_settings.heater_setpoint_valid) {
        return false;
    }
    *setpoint_f = s_settings.heater_setpoint_f;
    return true;
}

bool aquapilot_settings_set_heater_setpoint(float setpoint_f)
{
    if (!chihiros_setpoint_f_allowed(setpoint_f)) {
        return false;
    }
    s_settings.heater_setpoint_f = setpoint_f;
    s_settings.heater_setpoint_valid = 1;
    ESP_LOGI(TAG, "heater setpoint %.1f F", setpoint_f);
    return save_settings();
}

bool aquapilot_settings_get_shelly_address(aquapilot_shelly_plug_t plug, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return false;
    }

    char *slot = shelly_slot(&s_settings, plug);
    if (slot == NULL) {
        buf[0] = '\0';
        return false;
    }

    normalize_shelly_address(slot, buf, buf_len);
    return true;
}

bool aquapilot_settings_has_shelly_address(aquapilot_shelly_plug_t plug)
{
    char *slot = shelly_slot(&s_settings, plug);
    return slot != NULL && slot[0] != '\0';
}

bool aquapilot_settings_set_shelly_address(aquapilot_shelly_plug_t plug, const char *address)
{
    char *slot = shelly_slot(&s_settings, plug);
    if (slot == NULL || address == NULL) {
        return false;
    }

    char normalized[SHELLY_ADDR_LEN];
    normalize_shelly_address(address, normalized, sizeof(normalized));
    if (!shelly_address_valid(normalized)) {
        return false;
    }

    strncpy(slot, normalized, SHELLY_ADDR_LEN - 1);
    slot[SHELLY_ADDR_LEN - 1] = '\0';
    ESP_LOGI(TAG, "shelly %d address \"%s\"", (int)plug, slot);
    return save_settings();
}

bool aquapilot_settings_set_shelly_addresses(const char *heater, const char *filter, const char *co2)
{
    char norm_heater[SHELLY_ADDR_LEN];
    char norm_filter[SHELLY_ADDR_LEN];
    char norm_co2[SHELLY_ADDR_LEN];

    normalize_shelly_address(heater, norm_heater, sizeof(norm_heater));
    normalize_shelly_address(filter, norm_filter, sizeof(norm_filter));
    normalize_shelly_address(co2, norm_co2, sizeof(norm_co2));

    if (!shelly_address_valid(norm_heater) || !shelly_address_valid(norm_filter) ||
        !shelly_address_valid(norm_co2)) {
        return false;
    }

    strncpy(s_settings.shelly_heater, norm_heater, SHELLY_ADDR_LEN - 1);
    s_settings.shelly_heater[SHELLY_ADDR_LEN - 1] = '\0';
    strncpy(s_settings.shelly_filter, norm_filter, SHELLY_ADDR_LEN - 1);
    s_settings.shelly_filter[SHELLY_ADDR_LEN - 1] = '\0';
    strncpy(s_settings.shelly_co2, norm_co2, SHELLY_ADDR_LEN - 1);
    s_settings.shelly_co2[SHELLY_ADDR_LEN - 1] = '\0';
    ESP_LOGI(TAG, "shelly addresses heater=\"%s\" filter=\"%s\" co2=\"%s\"", s_settings.shelly_heater,
             s_settings.shelly_filter, s_settings.shelly_co2);
    return save_settings();
}

bool aquapilot_settings_get_feeder_host(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return false;
    }

    strncpy(buf, s_settings.feeder_host, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return true;
}

bool aquapilot_settings_has_feeder_host(void)
{
    return s_settings.feeder_host[0] != '\0';
}

bool aquapilot_settings_set_feeder_host(const char *host)
{
    char normalized[SHELLY_ADDR_LEN];
    normalize_shelly_address(host, normalized, sizeof(normalized));
    if (!shelly_address_valid(normalized)) {
        return false;
    }

    strncpy(s_settings.feeder_host, normalized, SHELLY_ADDR_LEN - 1);
    s_settings.feeder_host[SHELLY_ADDR_LEN - 1] = '\0';
    ESP_LOGI(TAG, "feeder host \"%s\"", s_settings.feeder_host);
    return save_settings();
}

bool aquapilot_settings_get_heater_override_enabled(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.heater_override_enabled != 0;
    return true;
}

bool aquapilot_settings_set_heater_override_enabled(bool enabled)
{
    s_settings.heater_override_enabled = enabled ? 1 : 0;
    ESP_LOGI(TAG, "heater override %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_co2_power_monitor_enabled(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.co2_power_monitor_enabled != 0;
    return true;
}

bool aquapilot_settings_set_co2_power_monitor_enabled(bool enabled)
{
    s_settings.co2_power_monitor_enabled = enabled ? 1 : 0;
    ESP_LOGI(TAG, "CO2 power monitor %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_heater_shelly_power_monitor_enabled(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.heater_shelly_power_monitor_enabled != 0;
    return true;
}

bool aquapilot_settings_set_heater_shelly_power_monitor_enabled(bool enabled)
{
    s_settings.heater_shelly_power_monitor_enabled = enabled ? 1 : 0;
    ESP_LOGI(TAG, "heater shelly power monitor %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_wifi_time_enabled(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.wifi_time_enabled != 0;
    return true;
}

bool aquapilot_settings_set_wifi_time_enabled(bool enabled)
{
    s_settings.wifi_time_enabled = enabled ? 1 : 0;
    ESP_LOGI(TAG, "wifi time %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_timezone(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return false;
    }
    strncpy(buf, s_settings.timezone, len - 1);
    buf[len - 1] = '\0';
    return true;
}

bool aquapilot_settings_set_timezone(const char *timezone)
{
    if (!timezone_valid(timezone)) {
        return false;
    }
    strncpy(s_settings.timezone, timezone, AQUAPILOT_TIMEZONE_MAX - 1);
    s_settings.timezone[AQUAPILOT_TIMEZONE_MAX - 1] = '\0';
    ESP_LOGI(TAG, "timezone \"%s\"", s_settings.timezone);
    return save_settings();
}

bool aquapilot_settings_get_manual_time_valid(bool *valid)
{
    if (valid == NULL) {
        return false;
    }
    *valid = s_settings.manual_time_valid != 0;
    return true;
}

bool aquapilot_settings_get_manual_epoch(int64_t *epoch)
{
    if (epoch == NULL || !s_settings.manual_time_valid) {
        return false;
    }
    *epoch = s_settings.manual_epoch;
    return true;
}

bool aquapilot_settings_set_manual_epoch(int64_t epoch)
{
    if (epoch < 0) {
        return false;
    }
    s_settings.manual_epoch = epoch;
    s_settings.manual_time_valid = epoch > 0 ? 1 : 0;
    ESP_LOGI(TAG, "manual epoch %lld", (long long)epoch);
    return save_settings();
}

bool aquapilot_settings_get_maintenance_mode_enabled(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.maintenance_mode_enabled != 0;
    return true;
}

bool aquapilot_settings_set_maintenance_mode_enabled(bool enabled)
{
    s_settings.maintenance_mode_enabled = enabled ? 1 : 0;
    ESP_LOGI(TAG, "maintenance mode %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_feeder_enabled(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.feeder_enabled != 0;
    return true;
}

bool aquapilot_settings_set_feeder_enabled(bool enabled)
{
    s_settings.feeder_enabled = enabled ? 1 : 0;
    ESP_LOGI(TAG, "feeder %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_feeder_schedule(uint8_t *start_h, uint8_t *start_m, uint8_t *end_h, uint8_t *end_m,
                                              uint8_t *times_per_day, uint16_t *amount_seconds)
{
    if (start_h == NULL || start_m == NULL || end_h == NULL || end_m == NULL || times_per_day == NULL ||
        amount_seconds == NULL) {
        return false;
    }

    *start_h = s_settings.feeder_start_h;
    *start_m = s_settings.feeder_start_m;
    *end_h = s_settings.feeder_end_h;
    *end_m = s_settings.feeder_end_m;
    *times_per_day = s_settings.feeder_times_per_day;
    *amount_seconds = s_settings.feeder_amount_seconds;
    return true;
}

bool aquapilot_settings_set_feeder_schedule(uint8_t start_h, uint8_t start_m, uint8_t end_h, uint8_t end_m,
                                            uint8_t times_per_day, uint16_t amount_seconds)
{
    if (start_h > 23 || start_m > 59 || end_h > 23 || end_m > 59) {
        return false;
    }
    if (times_per_day < FEEDER_TIMES_PER_DAY_MIN || times_per_day > FEEDER_TIMES_PER_DAY_MAX) {
        return false;
    }
    if (amount_seconds < FEEDER_AMOUNT_SECONDS_MIN || amount_seconds > FEEDER_AMOUNT_SECONDS_MAX) {
        return false;
    }
    if (start_h == end_h && start_m == end_m) {
        return false;
    }

    s_settings.feeder_start_h = start_h;
    s_settings.feeder_start_m = start_m;
    s_settings.feeder_end_h = end_h;
    s_settings.feeder_end_m = end_m;
    s_settings.feeder_times_per_day = times_per_day;
    s_settings.feeder_amount_seconds = amount_seconds;
    ESP_LOGI(TAG, "feeder schedule %02u:%02u–%02u:%02u, %u×/day, %us", start_h, start_m, end_h, end_m,
             (unsigned)times_per_day, (unsigned)amount_seconds);
    return save_settings();
}

bool aquapilot_settings_get_display_flip_180(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = s_settings.display_flip_180 != 0;
    return true;
}

bool aquapilot_settings_set_display_flip_180(bool enabled)
{
    s_settings.display_flip_180 = enabled ? 1 : 0;
    ESP_LOGI(TAG, "display flip 180° %s", enabled ? "enabled" : "disabled");
    return save_settings();
}

bool aquapilot_settings_get_display_brightness(uint8_t *brightness_pct)
{
    if (brightness_pct == NULL) {
        return false;
    }
    *brightness_pct = s_settings.display_brightness_pct;
    return true;
}

bool aquapilot_settings_set_display_brightness(uint8_t brightness_pct)
{
    if (brightness_pct < DISPLAY_BRIGHTNESS_MIN) {
        brightness_pct = DISPLAY_BRIGHTNESS_MIN;
    }
    if (brightness_pct > DISPLAY_BRIGHTNESS_MAX) {
        brightness_pct = DISPLAY_BRIGHTNESS_MAX;
    }

    s_settings.display_brightness_pct = brightness_pct;
    ESP_LOGI(TAG, "display brightness %u%%", (unsigned)brightness_pct);
    return save_settings();
}
