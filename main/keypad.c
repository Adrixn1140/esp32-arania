#include "keypad.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "keypad";

/* Keypad connector pins 1..7 -> ESP32 GPIOs 13,14,27,26,25,33,32.
 * Assumed HX-543 pinout: 1=R1, 2=R2, 3=R3, 4=R4, 5=C1, 6=C2, 7=C3.
 * If the keys come out mixed, swap the GPIOs / map below. */
static const gpio_num_t s_col_gpios[KEYPAD_COLS] = { GPIO_NUM_25, GPIO_NUM_33, GPIO_NUM_32 };
static const gpio_num_t s_row_gpios[KEYPAD_ROWS] = { GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26 };

static const char s_keys[KEYPAD_ROWS][KEYPAD_COLS] = {
    { '1', '2', '3' },
    { '4', '5', '6' },
    { '7', '8', '9' },
    { '*', '0', '#' },
};

void keypad_init(void)
{
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_row_gpios[r],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }

    for (int c = 0; c < KEYPAD_COLS; c++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_col_gpios[c],
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        gpio_set_level(s_col_gpios[c], 1);
    }

    ESP_LOGI(TAG, "init: rows=%d cols=%d", KEYPAD_ROWS, KEYPAD_COLS);
}

static int scan_matrix(int *out_col, int *out_row)
{
    for (int c = 0; c < KEYPAD_COLS; c++) {
        gpio_set_level(s_col_gpios[c], 0);
        for (int r = 0; r < KEYPAD_ROWS; r++) {
            if (gpio_get_level(s_row_gpios[r]) == 0) {
                *out_col = c;
                *out_row = r;
                gpio_set_level(s_col_gpios[c], 1);
                return 1;
            }
        }
        gpio_set_level(s_col_gpios[c], 1);
    }
    return 0;
}

char keypad_get_key(int *col, int *row)
{
    static char last_key = KEYPAD_NO_KEY;
    int hit_col, hit_row;
    char key;

    if (scan_matrix(&hit_col, &hit_row)) {
        key = s_keys[hit_row][hit_col];
        if (key != last_key) {
            last_key = key;
            *col = hit_col;
            *row = hit_row;
            return key;
        }
    } else {
        if (last_key != KEYPAD_NO_KEY) {
            ESP_LOGI(TAG, "key '%c' released", last_key);
        }
        last_key = KEYPAD_NO_KEY;
    }
    return KEYPAD_NO_KEY;
}