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
 * Postura sprawling (reptiliana): las patas salen hacia los lados, cada SG90 gira
 * sobre un eje vertical (yaw) y "adelante/atras" es un barrido horizontal. Las patas
 * izquierdas van montadas espejadas, por eso tienen invert=true.
 *
 * Marcha "wave": una pata a la vez, en bucle, orden contralateral FR -> BL -> FL -> BR.
 * Cada pata barre neutro -> adelante -> atras -> neutro mientras las otras 3 quedan
 * en neutro (0 grados).
 *
 * Teclado:
 *   '1' = iniciar/parar la marcha.
 *   '2' = invertir el sentido (adelante/atras).
 *   '3' = subir velocidad.
 *   '4' = bajar velocidad.
 *   '0' = parar y reposo total.
 */
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "pca9685.h"
#include "keypad.h"

static const char *TAG = "app_main";

#define I2C_SDA_GPIO   21
#define I2C_SCL_GPIO   22
#define I2C_FREQ_HZ    100000
#define I2C_TIMEOUT_MS 1000

#define SERVO_FREQ_HZ  50

enum {
    LEG_FR = 0, /* delantera derecha */
    LEG_BR,     /* trasera  derecha */
    LEG_FL,     /* delantera izquierda */
    LEG_BL,     /* trasera  izquierda */
    LEG_COUNT
};

typedef struct {
    uint8_t channel;
    const char *name;
    bool invert;
} leg_t;

static const leg_t LEGS[LEG_COUNT] = {
    [LEG_FR] = {12, "FR", false},
    [LEG_BR] = {13, "BR", false},
    [LEG_FL] = {14, "FL", true},
    [LEG_BL] = {15, "BL", true},
};

/* Order contralateral: alterna lados para maxima estabilidad */
static const int WAVE_ORDER[LEG_COUNT] = { LEG_FR, LEG_BL, LEG_FL, LEG_BR };

#define NEUTRAL_ANGLE  0
#define STEP_DEG       40
#define WAVE_STAGGER_MS 150

#define STEP_MS_MIN    4
#define STEP_MS_MAX    40
#define STEP_MS_DEFAULT 12
#define STEP_MS_KEY_STEP 2

#define SCAN_PERIOD_MS 20

static volatile bool g_walking = false;
static volatile int g_dir = 1;              /* +1 adelante, -1 atras */
static volatile uint16_t g_step_ms = STEP_MS_DEFAULT;

static esp_err_t scan_address(i2c_master_bus_handle_t bus, uint8_t addr)
{
    return i2c_master_probe(bus, addr, I2C_TIMEOUT_MS);
}

static void set_leg(i2c_master_dev_handle_t dev, int leg, int16_t degrees)
{
    int16_t angle = LEGS[leg].invert ? -degrees : degrees;
    ESP_ERROR_CHECK(pca9685_set_angle(dev, LEGS[leg].channel, angle));
}

static void rest_all(i2c_master_dev_handle_t dev)
{
    for (int i = 0; i < LEG_COUNT; i++) {
        set_leg(dev, i, NEUTRAL_ANGLE);
    }
    ESP_LOGI(TAG, "all legs to neutral (%d deg)", NEUTRAL_ANGLE);
}

/* Barre una pata de `from` a `to` a la velocidad actual. */
static void ramp_leg(i2c_master_dev_handle_t dev, int leg, int16_t from, int16_t to)
{
    int step = (to >= from) ? 1 : -1;
    for (int a = from;; a += step) {
        set_leg(dev, leg, (int16_t)a);
        vTaskDelay(pdMS_TO_TICKS(g_step_ms));
        if (a == to) {
            break;
        }
    }
}

/* Un ciclo completo de marcha wave: cada pata barre y vuelve al neutro. */
static void wave_cycle(i2c_master_dev_handle_t dev)
{
    int16_t fwd = (int16_t)(g_dir * STEP_DEG);

    for (int i = 0; i < LEG_COUNT; i++) {
        int leg = WAVE_ORDER[i];
        ESP_LOGI(TAG, "wave: leg '%s'", LEGS[leg].name);
        ramp_leg(dev, leg, NEUTRAL_ANGLE, fwd);
        ramp_leg(dev, leg, fwd, -fwd);
        ramp_leg(dev, leg, -fwd, NEUTRAL_ANGLE);
        vTaskDelay(pdMS_TO_TICKS(WAVE_STAGGER_MS));
    }
}

/* Unica tarea que toca los servos: camina en bucle o queda en reposo. */
static void walk_task(void *arg)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)arg;
    bool resting = false;

    while (true) {
        if (g_walking) {
            resting = false;
            wave_cycle(dev);
        } else {
            if (!resting) {
                rest_all(dev);
                resting = true;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
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

    xTaskCreate(walk_task, "walk", 4096, dev, 5, NULL);

    ESP_LOGI(TAG, "ready: press '1' to start/stop the wave gait");
    ESP_LOGI(TAG, "keys: 1=start/stop 2=reverse 3=faster 4=slower 0=rest");

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
            g_walking = !g_walking;
            ESP_LOGI(TAG, "gait %s", g_walking ? "START" : "STOP");
            break;
        case '2':
            g_dir = -g_dir;
            ESP_LOGI(TAG, "direction: %s", g_dir > 0 ? "forward" : "reverse");
            break;
        case '3':
            if (g_step_ms <= STEP_MS_MIN + STEP_MS_KEY_STEP) {
                g_step_ms = STEP_MS_MIN;
            } else {
                g_step_ms -= STEP_MS_KEY_STEP;
            }
            ESP_LOGI(TAG, "speed: %u ms/deg", g_step_ms);
            break;
        case '4':
            if (g_step_ms >= STEP_MS_MAX - STEP_MS_KEY_STEP) {
                g_step_ms = STEP_MS_MAX;
            } else {
                g_step_ms += STEP_MS_KEY_STEP;
            }
            ESP_LOGI(TAG, "speed: %u ms/deg", g_step_ms);
            break;
        case '0':
            g_walking = false;
            ESP_LOGI(TAG, "rest");
            break;
        default:
            ESP_LOGW(TAG, "key '%c' not assigned", key);
            break;
        }
    }
}
