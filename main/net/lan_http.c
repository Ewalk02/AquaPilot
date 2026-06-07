#include "lan_http.h"

#include "ble/ble_central_manager.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "light/fluval_ble.h"

static SemaphoreHandle_t s_http_lock;
static int64_t s_last_http_end_us;

bool lan_http_should_defer(void)
{
    return ble_central_manager_is_light_exclusive() || fluval_ble_is_poll_window_active();
}

bool lan_http_wait_until_ready(TickType_t max_wait_ticks)
{
    const TickType_t start = xTaskGetTickCount();
    while (lan_http_should_defer()) {
        if ((xTaskGetTickCount() - start) >= max_wait_ticks) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

void lan_http_pace(bool skip_gap)
{
    if (skip_gap || s_last_http_end_us == 0) {
        return;
    }

    const int64_t elapsed = esp_timer_get_time() - s_last_http_end_us;
    if (elapsed >= LAN_HTTP_MIN_GAP_US) {
        return;
    }

    const int64_t wait_ms = (LAN_HTTP_MIN_GAP_US - elapsed) / 1000 + 1;
    vTaskDelay(pdMS_TO_TICKS((TickType_t)wait_ms));
}

bool lan_http_acquire(TickType_t timeout_ticks)
{
    if (s_http_lock == NULL) {
        s_http_lock = xSemaphoreCreateMutex();
        if (s_http_lock == NULL) {
            return false;
        }
    }

    return xSemaphoreTake(s_http_lock, timeout_ticks) == pdTRUE;
}

void lan_http_release(void)
{
    if (s_http_lock != NULL) {
        xSemaphoreGive(s_http_lock);
    }
    s_last_http_end_us = esp_timer_get_time();
}
