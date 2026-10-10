/*
 * ESP32 DevKit 1 + PCA9685 (I2C GPIO21/22) + arana cuadrupeda 4x SG90 + teclado HX-543.
 *
 * Wiring:
 *   ESP32 GPIO21 -> PCA9685 SDA
 *   ESP32 GPIO22 -> PCA9685 SCL
 *   PCA9685 ch12 -> pata delantera derecha (FR)
 *           ch13 -> pata trasera  derecha (BR)
 *           ch14 -> pata delantera izquierda (FL)
 *           ch15 -> pata trasera  izquierda (BL)
 *   Teclado (conector 1..7) -> GPIO 13(R1) 14(R2) 27(R3) 26(R4) 25(C1) 33(C2) 32(C3)
 *
 * MODO CALIBRACION. Al encender hace una secuencia automatica moviendo cada pata una por
 * una (sin teclado) para comprobar que los 4 servos responden; luego queda en reposo y
 * espera las teclas. Con cada tecla solo se mueve la pata indicada, lento y volviendo al
 * neutro. Sirve para validar canal, sentido (invert) y rango mecanico de cada servo.
 *
 * Teclado:
 *   '1' = barre solo FR (ch12)
 *   '2' = barre solo BR (ch13)
 *   '3' = barre solo FL (ch14)
 *   '4' = barre solo BL (ch15)
 *   '5' = barre las 4 patas juntas, lento y sincronizado
 *   '0' = reposo (todas a neutro)
 *   Resto = no mueve nada.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "pca9685.h"
#include "keypad.h"
#include "legs.h"
#include "test_mode.h"

static const char *TAG = "app_main";

#define I2C_SDA_GPIO   21
#define I2C_SCL_GPIO   22
#define I2C_FREQ_HZ    100000
#define I2C_TIMEOUT_MS 1000

#define SERVO_FREQ_HZ  50

#define SCAN_PERIOD_MS 20

static esp_err_t scan_address(i2c_master_bus_handle_t bus, uint8_t addr)
{
    return i2c_master_probe(bus, addr, I2C_TIMEOUT_MS);
}

void app_main(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    if (scan_address(bus, PCA9685_ADDR_DEFAULT) != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685 not found at 0x%02X - check wiring/pull-ups", PCA9685_ADDR_DEFAULT);
    } else {
        ESP_LOGI(TAG, "PCA9685 detected at 0x%02X", PCA9685_ADDR_DEFAULT);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9685_ADDR_DEFAULT,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_ERROR_CHECK(pca9685_init(dev, SERVO_FREQ_HZ));

    keypad_init();
    legs_rest_all(dev);
    test_mode_boot_sequence(dev);

    ESP_LOGI(TAG, "modo calibracion: teclas 1=FR 2=BR 3=FL 4=BL 5=todas 0=reposo");

    while (true) {
        int col, row;
        char key = keypad_get_key(&col, &row);
        if (key == KEYPAD_NO_KEY) {
            vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
            continue;
        }

        ESP_LOGI(TAG, "key='%c' row=%d col=%d", key, row, col);

        switch (key) {
        case '1':
            test_mode_sweep_leg(dev, LEG_FR);
            break;
        case '2':
            test_mode_sweep_leg(dev, LEG_BR);
            break;
        case '3':
            test_mode_sweep_leg(dev, LEG_FL);
            break;
        case '4':
            test_mode_sweep_leg(dev, LEG_BL);
            break;
        case '5':
            test_mode_sweep_all(dev);
            break;
        case '0':
            legs_rest_all(dev);
            break;
        default:
            ESP_LOGW(TAG, "key '%c' not assigned in calibration mode", key);
            break;
        }
    }
}