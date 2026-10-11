#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea la tarea de marcha (no arranca el movimiento hasta gait_set_running(true)).
 */
void gait_start(i2c_master_dev_handle_t dev);

bool gait_is_running(void);
void gait_set_running(bool run);

/** @brief Alterna entre patron WAVE (una pata a la vez) y TROT (diagonales sincronizadas). */
void gait_toggle_pattern(void);
const char *gait_pattern_name(void);

void gait_faster(void);
void gait_slower(void);
uint16_t gait_step_ms(void);

#ifdef __cplusplus
}
#endif
