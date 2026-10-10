#include "pca9685.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "pca9685";

#define PCA9685_MODE1      0x00
#define PCA9685_MODE2      0x01
#define PCA9685_LED0_ON_L  0x06
#define PCA9685_PRESCALE   0xFE

#define MODE1_RESTART 0x80
#define MODE1_SLEEP   0x10
#define MODE1_AI      0x20  /* auto-increment: needed for burst writes */

#define MODE2_OUTDRV  0x04  /* totem-pole */

#define OSC_FREQ_HZ   25000000ULL
#define PWM_PERIOD    4096ULL

#define PCA9685_TIMEOUT_MS 1000

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), PCA9685_TIMEOUT_MS);
}

static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, PCA9685_TIMEOUT_MS);
}

static esp_err_t set_prescale(i2c_master_dev_handle_t dev, uint8_t prescale)
{
    esp_err_t err;
    uint8_t mode1;

    err = read_reg(dev, PCA9685_MODE1, &mode1);
    if (err != ESP_OK) {
        return err;
    }

    /* Enter sleep to change PRESCALE (datasheet requirement). */
    err = write_reg(dev, PCA9685_MODE1, (mode1 & ~MODE1_RESTART) | MODE1_SLEEP);
    if (err != ESP_OK) {
        return err;
    }

    err = write_reg(dev, PCA9685_PRESCALE, prescale);
    if (err != ESP_OK) {
        return err;
    }

    /* Restore mode with auto-increment + restart. */
    return write_reg(dev, PCA9685_MODE1, (mode1 | MODE1_AI | MODE1_RESTART) & ~MODE1_SLEEP);
}

esp_err_t pca9685_init(i2c_master_dev_handle_t dev, uint16_t freq_hz)
{
    esp_err_t err;

    if (freq_hz < 40 || freq_hz > 1000) {
        ESP_LOGE(TAG, "freq %u Hz out of range 40..1000", freq_hz);
        return ESP_ERR_INVALID_ARG;
    }

    err = write_reg(dev, PCA9685_MODE2, MODE2_OUTDRV);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write MODE2 failed: %s", esp_err_to_name(err));
        return err;
    }

    err = write_reg(dev, PCA9685_MODE1, MODE1_AI);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write MODE1 failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    /* prescale = round(OSC / (4096 * freq)) - 1, valid 3..255; 40..1000 Hz -> 61..5 */
    uint32_t prescale = (uint32_t)llround((double)OSC_FREQ_HZ / ((double)PWM_PERIOD * (double)freq_hz)) - 1;
    if (prescale < 3 || prescale > 255) {
        ESP_LOGE(TAG, "prescale %u out of range", (unsigned)prescale);
        return ESP_ERR_INVALID_ARG;
    }

    err = set_prescale(dev, (uint8_t)prescale);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write PRESCALE failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "init ok: freq=%u Hz prescale=%u", freq_hz, (unsigned)prescale);
    return ESP_OK;
}

static esp_err_t set_channel_counts(i2c_master_dev_handle_t dev, uint8_t channel, uint16_t on, uint16_t off)
{
    if (channel > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t reg = PCA9685_LED0_ON_L + 4u * channel;
    uint8_t buf[5] = {
        reg,
        (uint8_t)(on & 0xFF),
        (uint8_t)((on >> 8) & 0x0F),
        (uint8_t)(off & 0xFF),
        (uint8_t)((off >> 8) & 0x0F),
    };
    return i2c_master_transmit(dev, buf, sizeof(buf), PCA9685_TIMEOUT_MS);
}

esp_err_t pca9685_set_channel_us(i2c_master_dev_handle_t dev, uint8_t channel, uint16_t pulse_us)
{
    if (pulse_us > 20000) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t counts = (uint16_t)((uint32_t)pulse_us * PWM_PERIOD / 20000ULL);
    if (counts > 4095) {
        counts = 4095;
    }
    return set_channel_counts(dev, channel, 0, counts);
}

esp_err_t pca9685_set_angle(i2c_master_dev_handle_t dev, uint8_t channel, int16_t degrees)
{
    if (degrees < -90) {
        degrees = -90;
    } else if (degrees > 90) {
        degrees = 90;
    }
    /* SG90 centered: -90 deg -> 500 us, 0 deg -> 1450 us, +90 deg -> 2400 us */
    uint16_t pulse_us = 500 + (uint16_t)((uint32_t)(degrees + 90) * (2400 - 500) / 180);
    return pca9685_set_channel_us(dev, channel, pulse_us);
}

esp_err_t pca9685_channel_off(i2c_master_dev_handle_t dev, uint8_t channel)
{
    if (channel > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t reg = PCA9685_LED0_ON_L + 4u * channel;
    uint8_t buf[5] = {reg, 0, 0, 0, 0x10}; /* OFF bit set -> fully off */
    return i2c_master_transmit(dev, buf, sizeof(buf), PCA9685_TIMEOUT_MS);
}
