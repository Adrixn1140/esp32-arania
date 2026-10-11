#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_ADDR_DEFAULT 0x40

/**
 * @brief Initialize PCA9685: reset MODE1 and set PWM frequency (default 50 Hz).
 *
 * @param dev_handle  I2C master device handle (already added to the bus).
 * @param freq_hz     Output frequency in Hz (40..1000).
 */
esp_err_t pca9685_init(i2c_master_dev_handle_t dev_handle, uint16_t freq_hz);

/**
 * @brief Set a channel pulse width in microseconds (0 = fully off).
 *
 * @param channel  0..15
 * @param pulse_us Pulse high time in microseconds.
 */
esp_err_t pca9685_set_channel_us(i2c_master_dev_handle_t dev_handle, uint8_t channel, uint16_t pulse_us);

/**
 * @brief Set a channel angle for a standard servo, centered on 0 deg.
 *
 * Maps -90..+90 deg to 500..2400 us, so 0 deg -> 1450 us (center/rest).
 * Values outside the range are clamped.
 *
 * @param channel  0..15
 * @param degrees  -90..+90
 */
esp_err_t pca9685_set_angle(i2c_master_dev_handle_t dev_handle, uint8_t channel, int16_t degrees);

/**
 * @brief Set a channel fully off.
 */
esp_err_t pca9685_channel_off(i2c_master_dev_handle_t dev_handle, uint8_t channel);

/**
 * @brief Lee el pulso programado actualmente en un canal.
 *
 * @param[out] pulse_us  Duracion de pulso en microsegundos si la salida esta
 *                       activa. Si el canal esta en "full off" devuelve
 *                       ESP_ERR_INVALID_STATE y no toca `pulse_us`.
 */
esp_err_t pca9685_read_channel_us(i2c_master_dev_handle_t dev_handle, uint8_t channel, uint16_t *pulse_us);

#ifdef __cplusplus
}
#endif
