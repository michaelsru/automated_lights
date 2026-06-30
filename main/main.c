#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// GPIO map differs between dev board (ESP32) and PCB target (ESP32-C3-MINI-1)
#if CONFIG_IDF_TARGET_ESP32C3
#define PIR_PIN     GPIO_NUM_4
#define RED_PIN     GPIO_NUM_6
#define GREEN_PIN   GPIO_NUM_7
#else // ESP32 devkit
#define PIR_PIN     GPIO_NUM_33
#define RED_PIN     GPIO_NUM_26
#define GREEN_PIN   GPIO_NUM_27
#endif

#define NO_MOTION_TIMEOUT_MS 4000
#define POLL_MS              100

#define LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LEDC_TIMER   LEDC_TIMER_0
#define LEDC_FREQ    5000
#define LEDC_RES     LEDC_TIMER_13_BIT
#define LEDC_MAX     ((1 << 13) - 1)  // 8191

#define BREATH_STEP_MS  10
#define BREATH_STEP     16  // duty units per step (~5s full sweep)

static volatile bool motion = false;

static void set_duty(ledc_channel_t ch, uint32_t duty) {
    ledc_set_duty(LEDC_MODE, ch, duty);
    ledc_update_duty(LEDC_MODE, ch);
}

static void breathing_task(void *arg) {
    uint32_t duty = 0;

    while (1) {
        if (motion) {
            if (duty < LEDC_MAX) duty = (duty + BREATH_STEP > LEDC_MAX) ? LEDC_MAX : duty + BREATH_STEP;
        } else {
            if (duty > 0)        duty = (duty < BREATH_STEP) ? 0 : duty - BREATH_STEP;
        }
        set_duty(LEDC_CHANNEL_0, duty);
        set_duty(LEDC_CHANNEL_1, duty);
        vTaskDelay(pdMS_TO_TICKS(BREATH_STEP_MS));
    }
}

static void setup_ledc(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = LEDC_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_MODE,
        .timer_sel  = LEDC_TIMER,
        .hpoint     = 0,
        .gpio_num   = RED_PIN,
        .channel    = LEDC_CHANNEL_0,
        .duty       = 0,
    };
    ledc_channel_config(&ch);

    ch.gpio_num = GREEN_PIN;
    ch.channel  = LEDC_CHANNEL_1;
    ch.duty     = LEDC_MAX;
    ledc_channel_config(&ch);
}

void app_main(void) {
    gpio_reset_pin(PIR_PIN);
    gpio_set_direction(PIR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIR_PIN, GPIO_PULLDOWN_ONLY);

    setup_ledc();
    xTaskCreate(breathing_task, "breathing", 2048, NULL, 5, NULL);

    uint32_t low_ms = 0;

    while (1) {
        if (gpio_get_level(PIR_PIN)) {
            low_ms = 0;
            motion = true;
        } else {
            low_ms += POLL_MS;
            if (low_ms >= NO_MOTION_TIMEOUT_MS) {
                motion = false;
                low_ms = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
