/*
 * ESP32 DevKit 1 + PCA9685 (I2C GPIO21/22) + arana cuadrupeda 4x SG90 + teclado HX-543.
 *
 * Wiring:
 *   ESP32 GPIO21 -> PCA9685 SDA
 *   ESP32 GPIO22 -> PCA9685 SCL
 *   PCA9685 ch12 -> pata delantera derecha  (FR)
 *           ch13 -> pata delantera izquierda (FL)
 *           ch14 -> pata trasera  izquierda (BL)
 *           ch15 -> pata trasera  derecha  (BR)
 *   Teclado (conector 1..7) -> GPIO 13(R1) 14(R2) 27(R3) 26(R4) 25(C1) 33(C2) 32(C3)
 *
 * NEUTRO CAPTURADO Y PERSISTENTE (NVS). Al encender:
 *   1. Carga de NVS los offsets de neutro de cada pata (0 = pata estirada/natural).
 *   2. Si no existen, los captura automaticamente leyendo el pulso actual de los
 *      canales 12-15 del PCA9685 (la pose en la que dejaste las patas) y los guarda.
 *   3. legs_rest_all() posiciona las 4 patas solas en esa pose. Repite igual en cada
 *      arranque.
 *
 * Teclado:
 *   '1' = selecciona FR (ch12)         '2' = selecciona FL (ch13)
 *   '3' = selecciona BL (ch14)         '4' = selecciona BR (ch15)
 *   '*' = la pata seleccionada -5 deg respecto a su natural
 *   '#' = la pata seleccionada +5 deg
 *   '5' = captura la pose actual de la pata seleccionada como su nuevo 0 (y guarda)
 *   '0' = todas a su natural (0 relativo), quietas
 *   Resto = no mueve nada.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "pca9685.h"
#include "keypad.h"
#include "legs.h"
#include "gait.h"

static const char *TAG = "app_main";

#define NVS_NAMESPACE "legs"
#define NVS_KEY_OFF   "offsets"

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

#define POS_STEP_DEG 5

static int16_t g_angle[LEG_COUNT] = {0, 0, 0, 0};
static int g_selected = LEG_FR;

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

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS listo (%s)", NVS_NAMESPACE);
}

static esp_err_t nvs_save_offsets(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open fallo: %s", esp_err_to_name(err));
        return err;
    }
    int16_t blob[LEG_COUNT];
    for (int i = 0; i < LEG_COUNT; i++) {
        blob[i] = legs_offset_get(i);
    }
    err = nvs_set_blob(h, NVS_KEY_OFF, blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob/commit fallo: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t nvs_load_offsets(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (lectura) fallo: %s", esp_err_to_name(err));
        return err;
    }
    int16_t blob[LEG_COUNT];
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY_OFF, blob, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob fallo: %s", esp_err_to_name(err));
        return err;
    }
    if (len != sizeof(blob)) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (int i = 0; i < LEG_COUNT; i++) {
        legs_offset_set(i, blob[i]);
    }
    ESP_LOGI(TAG, "offsets cargados de NVS: FR=%d FL=%d BL=%d BR=%d",
             blob[LEG_FR], blob[LEG_FL], blob[LEG_BL], blob[LEG_BR]);
    return ESP_OK;
}

static bool try_capture_all(i2c_master_dev_handle_t dev)
{
    bool all_ok = true;
    for (int i = 0; i < LEG_COUNT; i++) {
        uint16_t us;
        esp_err_t err = pca9685_read_channel_us(dev, legs_channel(i), &us);
        if (err != ESP_OK || us < 500 || us > 2400) {
            ESP_LOGW(TAG, "pata '%s' ch%d sin pulso valido (err=%s us=%u): offset=0",
                     legs_name(i), legs_channel(i), esp_err_to_name(err), us);
            legs_offset_set(i, 0);
            all_ok = false;
            continue;
        }
        legs_capture_pulse(i, us);
        ESP_LOGI(TAG, "pata '%s' ch%d: pulso=%uus -> offset=%d",
                 legs_name(i), legs_channel(i), us, legs_offset_get(i));
    }
    return all_ok;
}

static void nvs_load_or_capture(i2c_master_dev_handle_t dev)
{
    if (nvs_load_offsets() == ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "no hay offsets en NVS: capturo la pose actual de los canales 12-15");
    if (try_capture_all(dev)) {
        if (nvs_save_offsets() == ESP_OK) {
            ESP_LOGI(TAG, "offsets guardados en NVS");
        }
    } else {
        ESP_LOGW(TAG, "alguna pata sin pose: quedan con offset 0; usa '5' para capturar");
    }
}

void app_main(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;

    nvs_init();

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
    if (err == ESP_OK) {
        nvs_load_or_capture(dev);
        legs_rest_all(dev);
    } else {
        ESP_LOGE(TAG, "pca9685_init fallo (%s): se omite el movimiento de servos pero",
                 esp_err_to_name(err));
        ESP_LOGE(TAG, "se prueba el teclado para no bloquear el arranque.");
    }

    keypad_init();
    gait_start(dev);

    ESP_LOGI(TAG, "posicion: 1-4=pata, '*'/#=5deg, '5'=capturar 0, '0'=natural");
    ESP_LOGI(TAG, "marcha:   6=iniciar/parar, 7=patron (WAVE/TROT), 8=mas rapido, 9=mas lento");
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
        case '6':
            gait_set_running(!gait_is_running());
            ESP_LOGI(TAG, "marcha %s (patron %s)",
                     gait_is_running() ? "START" : "STOP", gait_pattern_name());
            break;
        case '7':
            gait_toggle_pattern();
            break;
        case '8':
            gait_faster();
            break;
        case '9':
            gait_slower();
            break;
        case '0':
            gait_set_running(false);
            legs_rest_all(dev);
            for (int i = 0; i < LEG_COUNT; i++) {
                g_angle[i] = 0;
            }
            ESP_LOGI(TAG, "todas a natural 0 deg, quietas");
            break;
        case '1':
        case '2':
        case '3':
        case '4': {
            if (gait_is_running()) {
                ESP_LOGW(TAG, "marcha en curso: para con '6' antes de posicionar");
                break;
            }
            g_selected = key - '1';
            legs_set_angle(dev, g_selected, g_angle[g_selected]);
            ESP_LOGI(TAG, "seleccion: pata '%s' ch%d -> %d deg (queda quieta)",
                     legs_name(g_selected), legs_channel(g_selected), g_angle[g_selected]);
            break;
        }
        case '*': {
            if (gait_is_running()) {
                break;
            }
            g_angle[g_selected] -= POS_STEP_DEG;
            legs_set_angle(dev, g_selected, g_angle[g_selected]);
            ESP_LOGI(TAG, "pata '%s' ch%d -> %d deg", legs_name(g_selected),
                     legs_channel(g_selected), g_angle[g_selected]);
            break;
        }
        case '#': {
            if (gait_is_running()) {
                break;
            }
            g_angle[g_selected] += POS_STEP_DEG;
            legs_set_angle(dev, g_selected, g_angle[g_selected]);
            ESP_LOGI(TAG, "pata '%s' ch%d -> %d deg", legs_name(g_selected),
                     legs_channel(g_selected), g_angle[g_selected]);
            break;
        }
        case '5': {
            if (gait_is_running()) {
                break;
            }
            uint16_t us;
            esp_err_t r = pca9685_read_channel_us(dev, legs_channel(g_selected), &us);
            if (r != ESP_OK || us < 500 || us > 2400) {
                ESP_LOGW(TAG, "pata '%s' ch%d sin pulso valido (err=%s us=%u), no se captura",
                         legs_name(g_selected), legs_channel(g_selected),
                         esp_err_to_name(r), us);
                break;
            }
            legs_capture_pulse(g_selected, us);
            if (nvs_save_offsets() != ESP_OK) {
                ESP_LOGE(TAG, "no se pudo guardar el offset en NVS");
            }
            legs_set_angle(dev, g_selected, g_angle[g_selected]);
            ESP_LOGI(TAG, "pata '%s': pulso=%uus capturado como 0 (offset=%d) y guardado",
                     legs_name(g_selected), us, legs_offset_get(g_selected));
            break;
        }
        default:
            ESP_LOGW(TAG, "key '%c' not assigned", key);
            break;
        }
    }
}