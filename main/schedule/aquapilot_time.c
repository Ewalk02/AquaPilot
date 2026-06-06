#include "aquapilot_time.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi_manager.h"
#include "storage/aquapilot_settings.h"

static const char *TAG = "time";

#define MIN_VALID_EPOCH 1700000000L
#define SNTP_RETRY_MS   30000

static bool s_sntp_started;
static bool s_sync_task_started;

static void apply_timezone(void)
{
    char tz[AQUAPILOT_TIMEZONE_MAX];
    if (!aquapilot_settings_get_timezone(tz, sizeof(tz)) || tz[0] == '\0') {
        strncpy(tz, "UTC0", sizeof(tz) - 1);
    }

    setenv("TZ", tz, 1);
    tzset();
}

static void on_time_sync(struct timeval *tv)
{
    if (tv == NULL) {
        return;
    }

    ESP_LOGI(TAG, "SNTP sync OK (epoch %lld)", (long long)tv->tv_sec);
}

static void stop_sntp(void)
{
    if (!s_sntp_started) {
        return;
    }

    esp_sntp_stop();
    s_sntp_started = false;
    ESP_LOGI(TAG, "SNTP stopped");
}

static void start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started");
}

static void apply_manual_epoch(void)
{
    bool valid = false;
    int64_t epoch = 0;
    if (!aquapilot_settings_get_manual_time_valid(&valid) || !valid ||
        !aquapilot_settings_get_manual_epoch(&epoch) || epoch < MIN_VALID_EPOCH) {
        return;
    }

    struct timeval tv = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGI(TAG, "manual time applied (epoch %lld)", (long long)epoch);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    bool wifi_time = true;
    aquapilot_settings_get_wifi_time_enabled(&wifi_time);
    if (wifi_time) {
        start_sntp();
    }
}

static void time_sync_task(void *arg)
{
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(SNTP_RETRY_MS));

        bool wifi_time = true;
        if (!aquapilot_settings_get_wifi_time_enabled(&wifi_time) || !wifi_time) {
            continue;
        }
        if (!aquapilot_wifi_is_connected()) {
            continue;
        }
        if (aquapilot_time_is_ready()) {
            continue;
        }

        if (!s_sntp_started) {
            start_sntp();
            continue;
        }

        ESP_LOGW(TAG, "clock not set, restarting SNTP");
        esp_sntp_restart();
    }
}

static void start_sync_task(void)
{
    if (s_sync_task_started) {
        return;
    }

    if (xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create SNTP retry task");
        return;
    }

    s_sync_task_started = true;
}

void aquapilot_time_init(void)
{
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);
    start_sync_task();
    aquapilot_time_apply_settings();
}

void aquapilot_time_apply_settings(void)
{
    apply_timezone();

    bool wifi_time = true;
    aquapilot_settings_get_wifi_time_enabled(&wifi_time);

    if (wifi_time) {
        if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") != NULL) {
            start_sntp();
        }
    } else {
        stop_sntp();
        apply_manual_epoch();
    }
}

bool aquapilot_time_is_ready(void)
{
    return time(NULL) >= MIN_VALID_EPOCH;
}

void aquapilot_time_format_current(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (!aquapilot_time_is_ready()) {
        snprintf(buf, len, "Clock not set");
        return;
    }

    time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        snprintf(buf, len, "Clock not set");
        return;
    }

    snprintf(buf, len, "%04d-%02d-%02d %02d:%02u", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, (unsigned)local.tm_min);
}

bool aquapilot_time_set_manual_from_local(int year, int month, int day, int hour, int minute)
{
    struct tm local = {0};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    local.tm_isdst = -1;

    time_t epoch = mktime(&local);
    if (epoch < MIN_VALID_EPOCH) {
        return false;
    }

    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }

    aquapilot_settings_set_manual_epoch((int64_t)epoch);
    return true;
}
