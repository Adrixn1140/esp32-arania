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
 * MODO CALIBRACION CON DIAGNOSTICO. Al encender:
 *   1. Escanea el bus I2C y muestra todas las direcciones encontradas (para detectar
 *      si el PCA9685 responde en 0x40 o en otra).
 *   2. Si el PCA9685 no responde, avisa con el motivo mas probable y se queda esperando
 *      (no se reinicia en bucle).
 *   3. Si responde, mueve cada servo uno por uno (sin teclado) para comprobar que los 4
 *      responden; luego queda en reposo esperando teclas.
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
#include "driver/gpio.h"
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

/* Si tu placa PCA9685 tiene el pin OE desactivado por defecto (activo en LOW),
 * cablea OE a un GPIO del ESP32 y pon su numero aqui para que el firmware lo lleve
 * a LOW y habilite las salidas. -1 = no tocar OE (cableado a GND o con jumper). */
#define PCA9685_OE_GPIO (-1)

#define SCAN_PERIOD_MS 20

static void enable_outputs(void)
{
#if PCA9685_OE_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PCA9685_OE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PCA9685_OE_GPIO, 0); /* OE activo en LOW = salidas habilitadas */
    ESP_LOGI(TAG, "OE_GPIO %d -> LOW (salidas habilitadas)", PCA9685_OE_GPIO);
#endif
}

static void scan_bus(i2c_master_bus_handle_t bus)
{
    int found = 0;
    ESP_LOGI(TAG, "I2C scan (0x03..0x77)...");
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        esp_err_t err = i2c_master_probe(bus, addr, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "found device: 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGE(TAG, "NO devices found on I2C: revisa SDA->GPIO21, SCL->GPIO22, VDD->3V3 y GND");
    } else if (found == 1) {
        ESP_LOGI(TAG, "bus OK: 1 dispositivo encontrado (el esperado es PCA9685 en 0x40)");
    }
}

static void i2c_ok(void)
{
    ESP_LOGI(TAG, "hardware listo. Nota servos: requieren V+ del PCA9685 -> 5V externo,");
    ESP_LOGI(TAG, "GND a tierra comun, y el pin OE del PCA9685 en LOW (GND o GPIO).");
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

    scan_bus(bus);
    enable_outputs();

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9685_ADDR_DEFAULT,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t err = pca9685_init(dev, SERVO_FREQ_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pca9685_init fallo (%s): el PCA9685 no confirma los registros.",
                 esp_err_to_name(err));
        ESP_LOGE(TAG, "Motivos probables: placa sin power (VDD a 3V3 o bus muerto).");
        ESP_LOGE(TAG, "Revisa SDA/SCL/3V3/GND. Reinicia cuando corrijas.");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    keypad_init();
    legs_rest_all(dev);

    ESP_LOGI(TAG, "------ DIAGNOSTICO 0x40 OK: muevo cada servo uno a uno ------");
    test_mode_boot_sequence(dev);

    ESP_LOGI(TAG, "modo calibracion: teclas 1=FR 2=BR 3=FL 4=BL 5=todas 0=reposo");
    i2c_ok();

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