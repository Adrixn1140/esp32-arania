#include "gait.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "legs.h"

static const char *TAG = "gait";

#define GAIT_AMP_DEG      20
#define GAIT_STEP_MS_MIN  4
#define GAIT_STEP_MS_MAX  40
#define GAIT_STEP_DEFAULT 12
#define GAIT_STEP_KEY     2
#define WAVE_STAGGER_MS   120
#define IDLE_MS          50

/* Orden contralateral para la marcha wave */
static const int WAVE_ORDER[LEG_COUNT] = {LEG_FR, LEG_BL, LEG_FL, LEG_BR};
/* Pares diagonales para el trot (sincronizado) */
static const int TROT_A[2] = {LEG_FR, LEG_BL};
static const int TROT_B[2] = {LEG_FL, LEG_BR};

static volatile bool s_running = false;
static volatile bool s_wave = false;
static volatile uint16_t s_step_ms = GAIT_STEP_DEFAULT;

static void ramp_leg(i2c_master_dev_handle_t dev, int leg, int16_t from, int16_t to)
{
    int step = (to >= from) ? 1 : -1;
    for (int a = from;; a += step) {
        legs_set_angle(dev, leg, (int16_t)a);
        vTaskDelay(pdMS_TO_TICKS(s_step_ms));
        if (a == to) {
            break;
        }
    }
}

static void wave_cycle(i2c_master_dev_handle_t dev)
{
    int16_t amp = GAIT_AMP_DEG;
    for (int i = 0; i < LEG_COUNT; i++) {
        int leg = WAVE_ORDER[i];
        ramp_leg(dev, leg, 0, amp);
        ramp_leg(dev, leg, amp, -amp);
        ramp_leg(dev, leg, -amp, 0);
        vTaskDelay(pdMS_TO_TICKS(WAVE_STAGGER_MS));
    }
}

static void trot_half(i2c_master_dev_handle_t dev, int16_t a_from, int16_t a_to, int16_t b_from, int16_t b_to)
{
    int step = (a_to >= a_from) ? 1 : -1;
    int16_t a = a_from;
    int16_t b = b_from;
    int bstep = (b_to >= b_from) ? 1 : -1;
    for (;;) {
        legs_set_angle(dev, TROT_A[0], a);
        legs_set_angle(dev, TROT_A[1], a);
        legs_set_angle(dev, TROT_B[0], b);
        legs_set_angle(dev, TROT_B[1], b);
        vTaskDelay(pdMS_TO_TICKS(s_step_ms));
        if (a == a_to) {
            break;
        }
        a += step;
        b += bstep;
    }
}

static void trot_cycle(i2c_master_dev_handle_t dev)
{
    int16_t amp = GAIT_AMP_DEG;
    trot_half(dev, -amp, amp, amp, -amp);
    trot_half(dev, amp, -amp, -amp, amp);
}

static void gait_task(void *arg)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)arg;
    bool resting = false;

    while (true) {
        if (s_running) {
            resting = false;
            if (s_wave) {
                wave_cycle(dev);
            } else {
                trot_cycle(dev);
            }
        } else {
            if (!resting) {
                legs_rest_all(dev);
                resting = true;
            }
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
        }
    }
}

void gait_start(i2c_master_dev_handle_t dev)
{
    xTaskCreate(gait_task, "gait", 4096, dev, 5, NULL);
    ESP_LOGI(TAG, "gait listo (patron %s, %u ms/deg, amp %d deg)",
             s_wave ? "WAVE" : "TROT", s_step_ms, GAIT_AMP_DEG);
}

bool gait_is_running(void)
{
    return s_running;
}

void gait_set_running(bool run)
{
    s_running = run;
}

void gait_toggle_pattern(void)
{
    s_wave = !s_wave;
    ESP_LOGI(TAG, "patron: %s", s_wave ? "WAVE (una pata a la vez)" : "TROT (diagonales)");
}

const char *gait_pattern_name(void)
{
    return s_wave ? "WAVE" : "TROT";
}

void gait_faster(void)
{
    if (s_step_ms <= GAIT_STEP_MS_MIN + GAIT_STEP_KEY) {
        s_step_ms = GAIT_STEP_MS_MIN;
    } else {
        s_step_ms -= GAIT_STEP_KEY;
    }
    ESP_LOGI(TAG, "velocidad: %u ms/deg", s_step_ms);
}

void gait_slower(void)
{
    if (s_step_ms >= GAIT_STEP_MS_MAX - GAIT_STEP_KEY) {
        s_step_ms = GAIT_STEP_MS_MAX;
    } else {
        s_step_ms += GAIT_STEP_KEY;
    }
    ESP_LOGI(TAG, "velocidad: %u ms/deg", s_step_ms);
}

uint16_t gait_step_ms(void)
{
    return s_step_ms;
}
