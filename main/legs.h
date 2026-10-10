#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LEG_FR = 0, /* delantera derecha  (ch12) */
    LEG_BR,     /* trasera  derecha   (ch13) */
    LEG_FL,     /* delantera izquierda (ch14) */
    LEG_BL,     /* trasera  izquierda (ch15) */
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
 * @brief Lleva todas las patas a neutro 0 grados (postura sprawling).
 */
void legs_rest_all(i2c_master_dev_handle_t dev);

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