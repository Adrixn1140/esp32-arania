#include "legs.h"
#include "pca9685.h"
#include "esp_log.h"

static const char *TAG = "legs";

static const leg_t LEGS[LEG_COUNT] = {
    [LEG_FR] = {12, "FR", false},
    [LEG_FL] = {13, "FL", true},
    [LEG_BL] = {14, "BL", true},
    [LEG_BR] = {15, "BR", false},
};

static int16_t s_offset[LEG_COUNT] = {0};

esp_err_t legs_set_angle(i2c_master_dev_handle_t dev, int leg, int16_t degrees)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        ESP_LOGE(TAG, "invalid leg %d", leg);
        return ESP_ERR_INVALID_ARG;
    }
    int16_t commanded = degrees + s_offset[leg];
    int16_t angle = LEGS[leg].invert ? -commanded : commanded;
    return pca9685_set_angle(dev, LEGS[leg].channel, angle);
}

void legs_rest_all(i2c_master_dev_handle_t dev)
{
    for (int i = 0; i < LEG_COUNT; i++) {
        legs_set_angle(dev, i, NEUTRAL_ANGLE);
    }
    ESP_LOGI(TAG, "todas a neutro natural (0 deg, offsets %d/%d/%d/%d)",
             s_offset[LEG_FR], s_offset[LEG_FL], s_offset[LEG_BL], s_offset[LEG_BR]);
}

int16_t legs_offset_get(int leg)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        return 0;
    }
    return s_offset[leg];
}

esp_err_t legs_offset_set(int leg, int16_t degrees)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_offset[leg] = degrees;
    return ESP_OK;
}

esp_err_t legs_capture_pulse(int leg, uint16_t pulse_us)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    /* pulso -> angulo fisico A (misma ecuacion inversa que pca9685_set_angle) */
    int16_t physical = (int16_t)((int32_t)(pulse_us - 500) * 180 / 1900) - 90;
    if (physical < -90) {
        physical = -90;
    } else if (physical > 90) {
        physical = 90;
    }
    /* comando D tal que invert? -D : D reproduzca A */
    int16_t cmd = LEGS[leg].invert ? (int16_t)-physical : physical;
    s_offset[leg] = cmd;
    return ESP_OK;
}

const char *legs_name(int leg)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        return "?";
    }
    return LEGS[leg].name;
}

uint8_t legs_channel(int leg)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        return 0xFF;
    }
    return LEGS[leg].channel;
}