#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LEG_FR = 0, /* delantera derecha   (ch12) */
    LEG_FL,     /* delantera izquierda (ch13) */
    LEG_BL,     /* trasera  izquierda  (ch14) */
    LEG_BR,     /* trasera  derecha    (ch15) */
    LEG_COUNT
};

typedef struct {
    uint8_t channel;
    const char *name;
    bool invert; /* patas izquierdas montadas espejadas en postura sprawling */
} leg_t;

#define NEUTRAL_ANGLE 0

/**
 * @brief Mueve una pata a un angulo (el invert de la pata se aplica aqui).
 */
esp_err_t legs_set_angle(i2c_master_dev_handle_t dev, int leg, int16_t degrees);

/**
 * @brief Lleva todas las patas a neutro 0 grados (postura natural/sprawling).
 *
 * Con el offset de cada pata aplicado, 0 = la pose capturada como natural.
 */
void legs_rest_all(i2c_master_dev_handle_t dev);

/**
 * @brief Devuelve el offset de neutro de una pata (grados en espacio de comando).
 */
int16_t legs_offset_get(int leg);

/**
 * @brief Fija el offset de neutro de una pata.
 *
 * @param degrees  Valor que debe comportarse como 0 (pata estirada/natural).
 */
esp_err_t legs_offset_set(int leg, int16_t degrees);

/**
 * @brief Captura el neutro de una pata a partir del pulso actual de su canal.
 *
 * Lee el estado fisico actual del servo (pulse_us) y calcula el offset para que
 * legs_set_angle(dev, leg, 0) reproduzca exactamente esa pose.
 */
esp_err_t legs_capture_pulse(int leg, uint16_t pulse_us);

/**
 * @brief Nombre corto de una pata ("FR", "BR", "FL", "BL" o "?").
 */
const char *legs_name(int leg);

/**
 * @brief Canal PCA9685 de una pata (o 0xFF si invalida).
 */
uint8_t legs_channel(int leg);

#ifdef __cplusplus
}
#endif