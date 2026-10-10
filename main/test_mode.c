#include "test_mode.h"
#include "legs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "test_mode";

#define TEST_STEP_DEG   40
#define TEST_MS_PER_DEG 30
#define TEST_PAUSE_MS   300
#define BOOT_BETWEEN_MS 1500

static int ramp_leg(i2c_master_dev_handle_t dev, int leg, int16_t from, int16_t to)
{
    int errors = 0;
    int step = (to >= from) ? 1 : -1;
    for (int a = from;; a += step) {
        if (legs_set_angle(dev, leg, (int16_t)a) != ESP_OK) {
            errors++;
        }
        vTaskDelay(pdMS_TO_TICKS(TEST_MS_PER_DEG));
        if (a == to) {
            break;
        }
    }
    return errors;
}

static int ramp_all(i2c_master_dev_handle_t dev, int16_t from, int16_t to)
{
    int errors = 0;
    int step = (to >= from) ? 1 : -1;
    for (int a = from;; a += step) {
        for (int i = 0; i < LEG_COUNT; i++) {
            if (legs_set_angle(dev, i, (int16_t)a) != ESP_OK) {
                errors++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TEST_MS_PER_DEG));
        if (a == to) {
            break;
        }
    }
    return errors;
}

void test_mode_sweep_leg(i2c_master_dev_handle_t dev, int leg)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        ESP_LOGW(TAG, "invalid leg %d", leg);
        return;
    }
    ESP_LOGI(TAG, "test: pata '%s' ch%d (las demas quedan quietas)",
             legs_name(leg), legs_channel(leg));
    int e1 = ramp_leg(dev, leg, NEUTRAL_ANGLE, TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    int e2 = ramp_leg(dev, leg, TEST_STEP_DEG, -TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    int e3 = ramp_leg(dev, leg, -TEST_STEP_DEG, NEUTRAL_ANGLE);
    if (e1 + e2 + e3 > 0) {
        ESP_LOGW(TAG, "pata '%s': %d errores de I2C en el barrido", legs_name(leg), e1 + e2 + e3);
    }
    legs_rest_all(dev);
}

void test_mode_sweep_all(i2c_master_dev_handle_t dev)
{
    ESP_LOGI(TAG, "test: las 4 patas juntas, sincronizadas y lentas");
    int errors = 0;
    errors += ramp_all(dev, NEUTRAL_ANGLE, TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    errors += ramp_all(dev, TEST_STEP_DEG, -TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    errors += ramp_all(dev, -TEST_STEP_DEG, NEUTRAL_ANGLE);
    if (errors > 0) {
        ESP_LOGW(TAG, "sweep all: %d errores de I2C", errors);
    }
    legs_rest_all(dev);
}

void test_mode_boot_sequence(i2c_master_dev_handle_t dev)
{
    ESP_LOGI(TAG, "boot test: moviendo cada pata una por una (sin teclado)");
    for (int i = 0; i < LEG_COUNT; i++) {
        ESP_LOGI(TAG, "boot test: patas '%s' ch%d ...", legs_name(i), legs_channel(i));
        int errors = 0;
        errors += ramp_leg(dev, i, NEUTRAL_ANGLE, TEST_STEP_DEG);
        vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
        errors += ramp_leg(dev, i, TEST_STEP_DEG, -TEST_STEP_DEG);
        vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
        errors += ramp_leg(dev, i, -TEST_STEP_DEG, NEUTRAL_ANGLE);
        if (errors > 0) {
            ESP_LOGW(TAG, "pata '%s': %d errores de I2C", legs_name(i), errors);
        }
        if (i < LEG_COUNT - 1) {
            vTaskDelay(pdMS_TO_TICKS(BOOT_BETWEEN_MS));
        }
    }
    legs_rest_all(dev);
    ESP_LOGI(TAG, "boot test: listo, todas en reposo");
}