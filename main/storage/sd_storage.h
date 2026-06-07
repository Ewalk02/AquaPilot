#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/** Mount the board SD card (blocking). */
esp_err_t aquapilot_sd_mount(void);

/** Unmount and mount again after I/O errors. */
esp_err_t aquapilot_sd_remount(void);

bool aquapilot_sd_is_mounted(void);

/** Block until mount completes or timeout. */
bool aquapilot_sd_wait_ready(TickType_t timeout_ticks);

/**
 * Mount on a background task so boot is not blocked by format.
 * Returns immediately; poll aquapilot_sd_wait_ready() or aquapilot_sd_is_mounted().
 */
esp_err_t aquapilot_sd_start_mount_task(void);

/** Block until mount finishes (feeds task WDT during format). */
bool aquapilot_sd_ensure_mounted(uint32_t timeout_ms);

/**
 * Thread-safe file read/write with retries. Paths must be under the BSP mount point.
 * Returns true when the full size was transferred.
 */
bool aquapilot_sd_read_file(const char *path, void *data, size_t size);
bool aquapilot_sd_write_file(const char *path, const void *data, size_t size);
