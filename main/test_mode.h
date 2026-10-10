#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Barre una sola pata (lento): 0 -> +40 -> 0 -> -40 -> 0.
 * Las demas patas quedan quietas en neutro. Valida canal e invert.
 */
void test_mode_sweep_leg(i2c_master_dev_handle_t dev, int leg);

/**
 * @brief Barre las 4 patas juntas, lento y sincronizado.
 */
void test_mode_sweep_all(i2c_master_dev_handle_t dev);

/**
 * @brief Secuencia automatica al arrancar: barre cada pata una por una (sin teclado),
 * con pausa entre ellas, y termina en reposo.
 */
void test_mode_boot_sequence(i2c_master_dev_handle_t dev);

#ifdef __cplusplus
}
#endif