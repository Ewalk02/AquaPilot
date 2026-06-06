#include "sht3x_sensor.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "sht3x";

#define SHT3X_ADDR_GND       0x44
#define SHT3X_ADDR_VDD       0x45
#define SHT3X_CMD_MEAS_HIGH  0x2400
#define SHT3X_CMD_SOFT_RESET 0x30A2
#define SHT3X_MEAS_DELAY_MS  20
#define SHT3X_POLL_MS        3000
#define SHT3X_I2C_TIMEOUT_MS 200
#define SHT3X_BUS_WAIT_MS    500
#define SHT3X_MAX_ATTEMPTS   5

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_poll_task;
static SemaphoreHandle_t s_i2c_mutex;
static volatile bool s_has_reading;
static volatile float s_temp_c;
static uint8_t s_addr;
static char s_status[24] = "No sensor";

static uint8_t sht3x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static esp_err_t sht3x_bus_idle(void)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_bus_wait_all_done(s_bus, SHT3X_BUS_WAIT_MS);
}

static esp_err_t sht3x_send_cmd(uint16_t cmd)
{
    const uint8_t buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), SHT3X_I2C_TIMEOUT_MS);
}

static esp_err_t sht3x_read_once(float *temp_c, float *humidity_pct)
{
    if (s_dev == NULL || s_bus == NULL || temp_c == NULL || humidity_pct == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_i2c_mutex != NULL) {
        xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(SHT3X_BUS_WAIT_MS));
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < SHT3X_MAX_ATTEMPTS; attempt++) {
        err = sht3x_bus_idle();
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        err = sht3x_send_cmd(SHT3X_CMD_MEAS_HIGH);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(SHT3X_MEAS_DELAY_MS));

        err = sht3x_bus_idle();
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint8_t data[6] = {0};
        err = i2c_master_receive(s_dev, data, sizeof(data), SHT3X_I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (sht3x_crc8(data, 2) != data[2] || sht3x_crc8(data + 3, 2) != data[5]) {
            err = ESP_ERR_INVALID_CRC;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
        const uint16_t raw_hum = ((uint16_t)data[3] << 8) | data[4];

        *temp_c = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
        *humidity_pct = 100.0f * ((float)raw_hum / 65535.0f);
        err = ESP_OK;
        break;
    }

    if (s_i2c_mutex != NULL) {
        xSemaphoreGive(s_i2c_mutex);
    }

    return err;
}

static esp_err_t sht3x_probe_and_attach(uint8_t addr)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (i2c_master_probe(s_bus, addr, 100) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };

    return i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}

static esp_err_t sht3x_soft_reset(void)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_i2c_mutex != NULL) {
        xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(SHT3X_BUS_WAIT_MS));
    }

    esp_err_t err = sht3x_bus_idle();
    if (err == ESP_OK) {
        err = sht3x_send_cmd(SHT3X_CMD_SOFT_RESET);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if (s_i2c_mutex != NULL) {
        xSemaphoreGive(s_i2c_mutex);
    }

    return err;
}

static void sht3x_poll_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(500));

    while (true) {
        float temp_c = 0.0f;
        float humidity = 0.0f;
        const esp_err_t err = sht3x_read_once(&temp_c, &humidity);

        if (err == ESP_OK) {
            s_temp_c = temp_c;
            s_has_reading = true;
            strncpy(s_status, "OK", sizeof(s_status));
            s_status[sizeof(s_status) - 1] = '\0';
            ESP_LOGD(TAG, "ambient %.1f F, %.0f%% RH", (temp_c * 9.0f / 5.0f) + 32.0f, humidity);
        } else {
            s_has_reading = false;
            if (err == ESP_ERR_INVALID_CRC) {
                strncpy(s_status, "CRC error", sizeof(s_status));
            } else {
                strncpy(s_status, "Read error", sizeof(s_status));
            }
            s_status[sizeof(s_status) - 1] = '\0';
            ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(SHT3X_POLL_MS));
    }
}

esp_err_t sht3x_sensor_init(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
        strncpy(s_status, "I2C error", sizeof(s_status));
        s_status[sizeof(s_status) - 1] = '\0';
        return err;
    }

    s_bus = bsp_i2c_get_handle();
    if (s_bus == NULL) {
        ESP_LOGW(TAG, "I2C bus handle unavailable");
        strncpy(s_status, "I2C error", sizeof(s_status));
        s_status[sizeof(s_status) - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    if (s_i2c_mutex == NULL) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (s_i2c_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    err = sht3x_probe_and_attach(SHT3X_ADDR_GND);
    s_addr = SHT3X_ADDR_GND;
    if (err == ESP_ERR_NOT_FOUND) {
        err = sht3x_probe_and_attach(SHT3X_ADDR_VDD);
        s_addr = SHT3X_ADDR_VDD;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHT3x not found on I2C bus");
        strncpy(s_status, "No sensor", sizeof(s_status));
        s_status[sizeof(s_status) - 1] = '\0';
        return err;
    }

    ESP_LOGI(TAG, "SHT3x found at 0x%02X", s_addr);

    err = sht3x_soft_reset();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "soft reset failed: %s", esp_err_to_name(err));
    }

    if (s_poll_task == NULL) {
        if (xTaskCreate(sht3x_poll_task, "sht3x_poll", 3072, NULL, 3, &s_poll_task) != pdPASS) {
            ESP_LOGW(TAG, "poll task create failed");
            strncpy(s_status, "Task error", sizeof(s_status));
            s_status[sizeof(s_status) - 1] = '\0';
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

bool sht3x_sensor_has_reading(void)
{
    return s_has_reading;
}

float sht3x_sensor_get_temp_f(void)
{
    return (s_temp_c * 9.0f / 5.0f) + 32.0f;
}

const char *sht3x_sensor_status_text(void)
{
    return s_status;
}
