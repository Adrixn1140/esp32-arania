#include "legs.h"
#include "pca9685.h"
#include "esp_log.h"

static const char *TAG = "legs";

static const leg_t LEGS[LEG_COUNT] = {
    [LEG_FR] = {12, "FR", false},
    [LEG_BR] = {13, "BR", false},
    [LEG_FL] = {14, "FL", true},
    [LEG_BL] = {15, "BL", true},
};

esp_err_t legs_set_angle(i2c_master_dev_handle_t dev, int leg, int16_t degrees)
{
    if (leg < 0 || leg >= LEG_COUNT) {
        ESP_LOGE(TAG, "invalid leg %d", leg);
        return ESP_ERR_INVALID_ARG;
    }
    int16_t angle = LEGS[leg].invert ? -degrees : degrees;
    return pca9685_set_angle(dev, LEGS[leg].channel, angle);
}

void legs_rest_all(i2c_master_dev_handle_t dev)
{
    for (int i = 0; i < LEG_COUNT; i++) {
        legs_set_angle(dev, i, NEUTRAL_ANGLE);
    }
    ESP_LOGI(TAG, "all legs to neutral (%d deg)", NEUTRAL_ANGLE);
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