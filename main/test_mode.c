#include "test_mode.h"
#include "legs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "test_mode";

#define TEST_STEP_DEG   40
#define TEST_MS_PER_DEG 30
#define TEST_PAUSE_MS   300

static void ramp_leg(i2c_master_dev_handle_t dev, int leg, int16_t from, int16_t to)
{
    int step = (to >= from) ? 1 : -1;
    for (int a = from;; a += step) {
        legs_set_angle(dev, leg, (int16_t)a);
        vTaskDelay(pdMS_TO_TICKS(TEST_MS_PER_DEG));
        if (a == to) {
            break;
        }
    }
}

static void ramp_all(i2c_master_dev_handle_t dev, int16_t from, int16_t to)
{
    int step = (to >= from) ? 1 : -1;
    for (int a = from;; a += step) {
        for (int i = 0; i < LEG_COUNT; i++) {
            legs_set_angle(dev, i, (int16_t)a);
        }
        vTaskDelay(pdMS_TO_TICKS(TEST_MS_PER_DEG));
        if (a == to) {
            break;
        }
    }
}

void test_mode_sweep_leg(i2c_master_dev_handle_t dev, int leg)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        ESP_LOGW(TAG, "invalid leg %d", leg);
        return;
    }
    ESP_LOGI(TAG, "test: pata '%s' ch%d (las demas quedan quietas)",
             legs_name(leg), legs_channel(leg));
    ramp_leg(dev, leg, NEUTRAL_ANGLE, TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    ramp_leg(dev, leg, TEST_STEP_DEG, -TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    ramp_leg(dev, leg, -TEST_STEP_DEG, NEUTRAL_ANGLE);
    legs_rest_all(dev);
}

void test_mode_sweep_all(i2c_master_dev_handle_t dev)
{
    ESP_LOGI(TAG, "test: las 4 patas juntas, sincronizadas y lentas");
    ramp_all(dev, NEUTRAL_ANGLE, TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    ramp_all(dev, TEST_STEP_DEG, -TEST_STEP_DEG);
    vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    ramp_all(dev, -TEST_STEP_DEG, NEUTRAL_ANGLE);
    legs_rest_all(dev);
}