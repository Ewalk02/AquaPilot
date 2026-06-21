#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void aquapilot_settings_init(void);

#define AQUAPILOT_SHELLY_ADDR_MAX     64
#define AQUAPILOT_SHELLY_PASSWORD_MAX 32
#define AQUAPILOT_FEEDER_HOST_MAX 64
#define AQUAPILOT_TIMEZONE_MAX    32

typedef enum {
    AQUAPILOT_SHELLY_HEATER = 0,
    AQUAPILOT_SHELLY_FILTER,
    AQUAPILOT_SHELLY_CO2,
} aquapilot_shelly_plug_t;

bool aquapilot_settings_get_temp_range(float *min_f, float *max_f);
bool aquapilot_settings_temp_contains(float temp_f);

bool aquapilot_settings_get_temp_deltas(float *delta_plus_f, float *delta_minus_f);
bool aquapilot_settings_set_temp_deltas(float delta_plus_f, float delta_minus_f);

bool aquapilot_settings_get_co2_schedule(uint8_t *on_h, uint8_t *on_m, uint8_t *off_h, uint8_t *off_m);
bool aquapilot_settings_set_co2_schedule(uint8_t on_h, uint8_t on_m, uint8_t off_h, uint8_t off_m);

bool aquapilot_settings_get_filter_calibrated(bool *calibrated);
bool aquapilot_settings_set_filter_calibrated(bool calibrated);
bool aquapilot_settings_get_filter_baseline_watts(float *watts);
bool aquapilot_settings_set_filter_baseline_watts(float watts);
bool aquapilot_settings_get_filter_bands(uint8_t *green_pct, uint8_t *yellow_pct, uint8_t *red_pct,
                                         uint8_t *red_cutoff_pct);
bool aquapilot_settings_set_filter_bands(uint8_t green_pct, uint8_t yellow_pct, uint8_t red_pct,
                                         uint8_t red_cutoff_pct);

bool aquapilot_settings_has_heater_setpoint(void);
bool aquapilot_settings_get_heater_setpoint(float *setpoint_f);
bool aquapilot_settings_set_heater_setpoint(float setpoint_f);

bool aquapilot_settings_get_shelly_address(aquapilot_shelly_plug_t plug, char *buf, size_t buf_len);
bool aquapilot_settings_has_shelly_address(aquapilot_shelly_plug_t plug);
bool aquapilot_settings_set_shelly_address(aquapilot_shelly_plug_t plug, const char *address);
bool aquapilot_settings_set_shelly_addresses(const char *heater, const char *filter, const char *co2);

/** Gen2/3/4 digest auth password (username is always "admin"). Empty = no auth. */
bool aquapilot_settings_get_shelly_password(char *buf, size_t buf_len);
bool aquapilot_settings_set_shelly_password(const char *password);

bool aquapilot_settings_get_feeder_host(char *buf, size_t buf_len);
bool aquapilot_settings_has_feeder_host(void);
bool aquapilot_settings_set_feeder_host(const char *host);

bool aquapilot_settings_get_heater_override_enabled(bool *enabled);
bool aquapilot_settings_set_heater_override_enabled(bool enabled);

bool aquapilot_settings_get_co2_power_monitor_enabled(bool *enabled);
bool aquapilot_settings_set_co2_power_monitor_enabled(bool enabled);

bool aquapilot_settings_get_heater_shelly_power_monitor_enabled(bool *enabled);
bool aquapilot_settings_set_heater_shelly_power_monitor_enabled(bool enabled);

bool aquapilot_settings_get_maintenance_mode_enabled(bool *enabled);
bool aquapilot_settings_set_maintenance_mode_enabled(bool enabled);

bool aquapilot_settings_get_feeder_enabled(bool *enabled);
bool aquapilot_settings_set_feeder_enabled(bool enabled);
bool aquapilot_settings_get_feeder_schedule(uint8_t *start_h, uint8_t *start_m, uint8_t *end_h, uint8_t *end_m,
                                              uint8_t *times_per_day, uint16_t *amount_tenths);
bool aquapilot_settings_set_feeder_schedule(uint8_t start_h, uint8_t start_m, uint8_t end_h, uint8_t end_m,
                                            uint8_t times_per_day, uint16_t amount_tenths);

bool aquapilot_settings_get_wifi_time_enabled(bool *enabled);
bool aquapilot_settings_set_wifi_time_enabled(bool enabled);
bool aquapilot_settings_get_timezone(char *buf, size_t len);
bool aquapilot_settings_set_timezone(const char *timezone);
bool aquapilot_settings_get_manual_time_valid(bool *valid);
bool aquapilot_settings_get_manual_epoch(int64_t *epoch);
bool aquapilot_settings_set_manual_epoch(int64_t epoch);

bool aquapilot_settings_get_display_flip_180(bool *enabled);
bool aquapilot_settings_set_display_flip_180(bool enabled);
bool aquapilot_settings_get_display_brightness(uint8_t *brightness_pct);
bool aquapilot_settings_set_display_brightness(uint8_t brightness_pct);

bool aquapilot_settings_get_temp_graph_logging_enabled(bool *enabled);
bool aquapilot_settings_set_temp_graph_logging_enabled(bool enabled);

#define AQUAPILOT_MAINT_ACTIVITY_COUNT 5
#define AQUAPILOT_MAINT_CUSTOM_MAX       8
#define AQUAPILOT_MAINT_CUSTOM_NAME_LEN 28

typedef struct {
    char name[AQUAPILOT_MAINT_CUSTOM_NAME_LEN];
    uint8_t interval_days;
    int32_t next_due_epoch;
} aquapilot_maint_custom_t;

bool aquapilot_settings_get_maint_next_due(int activity, int32_t *epoch);
bool aquapilot_settings_set_maint_next_due(int activity, int32_t epoch);
bool aquapilot_settings_update_maint_next_due(int activity, int32_t epoch);
bool aquapilot_settings_get_maint_interval_days(int activity, uint8_t *days);
bool aquapilot_settings_set_maint_interval_days(int activity, uint8_t days);
bool aquapilot_settings_update_maint_interval_days(int activity, uint8_t days);
bool aquapilot_settings_get_maint_custom(int slot, aquapilot_maint_custom_t *out);
bool aquapilot_settings_set_maint_custom(int slot, const aquapilot_maint_custom_t *item);
bool aquapilot_settings_update_maint_custom(int slot, const aquapilot_maint_custom_t *item);
int aquapilot_settings_find_free_maint_custom_slot(void);
bool aquapilot_settings_commit(void);

#define AQUAPILOT_THINGSPEAK_KEY_MAX     16
#define AQUAPILOT_THINGSPEAK_CHANNEL_MAX 12
#define AQUAPILOT_THINGSPEAK_FIELD_COUNT 8

typedef enum {
    AQUAPILOT_TS_METRIC_NONE = 0,
    AQUAPILOT_TS_METRIC_TEMP_F,
    AQUAPILOT_TS_METRIC_FILTER_W,
    AQUAPILOT_TS_METRIC_CO2_W,
    AQUAPILOT_TS_METRIC_FEED_STATUS,
} aquapilot_ts_metric_t;

bool aquapilot_settings_get_thingspeak_enabled(bool *enabled);
bool aquapilot_settings_set_thingspeak_enabled(bool enabled);
bool aquapilot_settings_get_thingspeak_api_key(char *buf, size_t buf_len);
bool aquapilot_settings_set_thingspeak_api_key(const char *key);
bool aquapilot_settings_get_thingspeak_channel_id(char *buf, size_t buf_len);
bool aquapilot_settings_set_thingspeak_channel_id(const char *channel_id);
bool aquapilot_settings_get_thingspeak_field(uint8_t field_idx, aquapilot_ts_metric_t *metric,
                                             uint16_t *interval_min);
bool aquapilot_settings_set_thingspeak_field(uint8_t field_idx, aquapilot_ts_metric_t metric,
                                             uint16_t interval_min);
