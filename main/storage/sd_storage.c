#include "sd_storage.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_task_wdt.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "sd_storage";

#define SD_IO_RETRIES        3
#define SD_IO_RETRY_DELAY_MS 100

static bool s_mounted;
static bool s_mount_started;
static bool s_mount_done;
static SemaphoreHandle_t s_io_mutex;
static TaskHandle_t s_mount_task;
static sd_pwr_ctrl_handle_t s_pwr_ctrl;

static esp_err_t mount_once(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;

    if (s_pwr_ctrl == NULL) {
        const sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = 4,
        };
        const esp_err_t pwr_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl);
        if (pwr_err != ESP_OK) {
            ESP_LOGE(TAG, "SD LDO power control failed: %s", esp_err_to_name(pwr_err));
            return pwr_err;
        }
    }
    host.pwr_ctrl_handle = s_pwr_ctrl;

    const sdmmc_slot_config_t slot_config = {
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };

    static const int freq_khz[] = {
        SDMMC_FREQ_DEFAULT,
        SDMMC_FREQ_HIGHSPEED,
        SDMMC_FREQ_PROBING,
    };

    esp_err_t ret;
    esp_err_t last_err = ESP_FAIL;
    for (size_t i = 0; i < sizeof(freq_khz) / sizeof(freq_khz[0]); i++) {
        host.max_freq_khz = freq_khz[i];
        ESP_LOGI(TAG, "trying SD mount at %d kHz", freq_khz[i]);

        ret = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &bsp_sdcard);
        if (ret == ESP_OK) {
            s_mounted = true;
            ESP_LOGI(TAG, "SD card mounted at %s (%s)", BSP_SD_MOUNT_POINT, bsp_sdcard->cid.name);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "SD mount at %d kHz failed: %s", freq_khz[i], esp_err_to_name(ret));
        if (bsp_sdcard != NULL) {
            (void)esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
            bsp_sdcard = NULL;
        }
        last_err = ret;
    }

    ESP_LOGE(TAG, "SD card mount failed after all attempts");
    return last_err;
}

static void ensure_io_mutex(void)
{
    if (s_io_mutex == NULL) {
        s_io_mutex = xSemaphoreCreateMutex();
    }
}

static bool lock_io(TickType_t timeout_ticks)
{
    ensure_io_mutex();
    if (s_io_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_io_mutex, timeout_ticks) == pdTRUE;
}

static void unlock_io(void)
{
    if (s_io_mutex != NULL) {
        xSemaphoreGive(s_io_mutex);
    }
}

esp_err_t aquapilot_sd_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    ensure_io_mutex();
    return mount_once();
}

esp_err_t aquapilot_sd_remount(void)
{
    if (!lock_io(pdMS_TO_TICKS(5000))) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_mounted && bsp_sdcard != NULL) {
        ESP_LOGW(TAG, "remounting SD card");
        (void)esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
        bsp_sdcard = NULL;
        s_mounted = false;
    }

    const esp_err_t err = mount_once();
    unlock_io();
    return err;
}

bool aquapilot_sd_is_mounted(void)
{
    return s_mounted;
}

bool aquapilot_sd_wait_ready(TickType_t timeout_ticks)
{
    const TickType_t start = xTaskGetTickCount();
    while (!s_mount_done) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return s_mounted;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return s_mounted;
}

static void mount_task(void *arg)
{
    (void)arg;

    const esp_err_t err = aquapilot_sd_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "background mount failed: %s", esp_err_to_name(err));
    }

    s_mount_done = true;
    s_mount_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t aquapilot_sd_start_mount_task(void)
{
    if (s_mounted || s_mount_started) {
        return ESP_OK;
    }

    ensure_io_mutex();
    s_mount_started = true;
    s_mount_done = false;

    if (xTaskCreate(mount_task, "sd_mount", 10240, NULL, 8, &s_mount_task) != pdPASS) {
        s_mount_started = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool aquapilot_sd_ensure_mounted(uint32_t timeout_ms)
{
    if (s_mounted) {
        return true;
    }

    if (!s_mount_started) {
        if (aquapilot_sd_start_mount_task() != ESP_OK) {
            ESP_LOGE(TAG, "could not start mount task");
            return false;
        }
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (!s_mount_done) {
        esp_task_wdt_reset();
        if (xTaskGetTickCount() >= deadline) {
            ESP_LOGE(TAG, "mount wait timed out (mounted=%d)", (int)s_mounted);
            return s_mounted;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return s_mounted;
}

static bool transfer_file(const char *path, void *data, size_t size, bool write)
{
    if (!s_mounted || path == NULL || data == NULL || size == 0) {
        return false;
    }

    for (int attempt = 0; attempt < SD_IO_RETRIES; attempt++) {
        if (!lock_io(pdMS_TO_TICKS(3000))) {
            continue;
        }

        bool ok = false;
        FILE *f = fopen(path, write ? "wb" : "rb");
        if (f == NULL) {
            const int open_errno = errno;
            ESP_LOGW(TAG, "%s open %s failed (errno %d: %s)", write ? "write" : "read", path, open_errno,
                     strerror(open_errno));
            unlock_io();
            if (!write && (open_errno == ENOENT || open_errno == EINVAL)) {
                return false;
            }
            if (attempt + 1 < SD_IO_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(SD_IO_RETRY_DELAY_MS));
            }
            continue;
        }

        const size_t transferred = write ? fwrite(data, 1, size, f) : fread(data, 1, size, f);
        if (transferred == size) {
            fflush(f);
            fsync(fileno(f));
            ok = true;
        } else {
            ESP_LOGW(TAG, "%s %s short transfer (%u/%u bytes)", write ? "write" : "read", path,
                     (unsigned)transferred, (unsigned)size);
        }

        fclose(f);
        unlock_io();

        if (ok) {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(SD_IO_RETRY_DELAY_MS));
    }

    ESP_LOGW(TAG, "%s %s failed after %d attempts, trying remount", write ? "write" : "read", path, SD_IO_RETRIES);
    if (aquapilot_sd_remount() != ESP_OK) {
        return false;
    }

    if (!lock_io(pdMS_TO_TICKS(3000))) {
        return false;
    }

    bool ok = false;
    FILE *f = fopen(path, write ? "wb" : "rb");
    if (f != NULL) {
        const size_t transferred = write ? fwrite(data, 1, size, f) : fread(data, 1, size, f);
        if (transferred == size) {
            fflush(f);
            fsync(fileno(f));
            ok = true;
        }
        fclose(f);
    } else {
        ESP_LOGW(TAG, "%s open %s failed after remount (errno %d: %s)", write ? "write" : "read", path, errno,
                 strerror(errno));
    }

    unlock_io();
    return ok;
}

bool aquapilot_sd_read_file(const char *path, void *data, size_t size)
{
    return transfer_file(path, data, size, false);
}

bool aquapilot_sd_write_file(const char *path, const void *data, size_t size)
{
    return transfer_file(path, (void *)data, size, true);
}
