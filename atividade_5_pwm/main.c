#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#define BUTTON_A_PIN    GPIO_NUM_4
#define BUTTON_B_PIN    GPIO_NUM_5

#define LED_BIT0_PIN    GPIO_NUM_2
#define LED_BIT1_PIN    GPIO_NUM_42
#define LED_BIT2_PIN    GPIO_NUM_40
#define LED_BIT3_PIN    GPIO_NUM_39

#define LED_PWM_PIN     GPIO_NUM_18
#define BUZZER_PWM_PIN  GPIO_NUM_19

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_LED        LEDC_CHANNEL_0
#define LEDC_CHANNEL_BUZZER     LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT 
#define LEDC_FREQUENCY          (1000)           

// Configuracoes de Debounce
#define DEBOUNCE_TIME_US        (200000)

static QueueHandle_t gpio_evt_queue = NULL;

static uint8_t g_counter = 0;

// ISR para os botoes
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void update_binary_leds(uint8_t value) {
    gpio_set_level(LED_BIT0_PIN, (value >> 0) & 0x01);
    gpio_set_level(LED_BIT1_PIN, (value >> 1) & 0x01);
    gpio_set_level(LED_BIT2_PIN, (value >> 2) & 0x01);
    gpio_set_level(LED_BIT3_PIN, (value >> 3) & 0x01);
}

static void update_pwm(uint8_t value) {
    uint32_t max_duty = (1 << 13) - 1;
    uint32_t duty = (value * max_duty) / 15;

    // Atualiza Duty Cycle do LED PWM
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LED, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LED);

    // Atualiza Duty Cycle do Buzzer
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER);
}

static void button_task(void* arg) {
    uint32_t io_num;
    int64_t last_time_a = 0;
    int64_t last_time_b = 0;

    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int64_t now = esp_timer_get_time();

            if (io_num == BUTTON_A_PIN) {
                if ((now - last_time_a) > DEBOUNCE_TIME_US) {
                    if (gpio_get_level(BUTTON_A_PIN) == 0) { 
                        g_counter = (g_counter + 1) & 0x0F;  
                        update_binary_leds(g_counter);
                        update_pwm(g_counter);
                        printf("Botao A pressionado | Contador: %d (0x%X)\n", g_counter, g_counter);
                    }
                    last_time_a = now;
                }
            } else if (io_num == BUTTON_B_PIN) {
                if ((now - last_time_b) > DEBOUNCE_TIME_US) {
                    if (gpio_get_level(BUTTON_B_PIN) == 0) { 
                        g_counter = (g_counter - 1) & 0x0F; 
                        update_binary_leds(g_counter);
                        update_pwm(g_counter);
                        printf("Botao B pressionado | Contador: %d (0x%X)\n", g_counter, g_counter);
                    }
                    last_time_b = now;
                }
            }
        }
    }
}

static void init_gpio(void) {
    gpio_config_t io_conf_leds = {
        .pin_bit_mask = (1ULL << LED_BIT0_PIN) | (1ULL << LED_BIT1_PIN) |
                        (1ULL << LED_BIT2_PIN) | (1ULL << LED_BIT3_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_leds);

    gpio_config_t io_conf_buttons = {
        .pin_bit_mask = (1ULL << BUTTON_A_PIN) | (1ULL << BUTTON_B_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_conf_buttons);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_A_PIN, gpio_isr_handler, (void*) BUTTON_A_PIN);
    gpio_isr_handler_add(BUTTON_B_PIN, gpio_isr_handler, (void*) BUTTON_B_PIN);
}

static void init_pwm(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel_led = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_LED,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_PWM_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel_led);

    ledc_channel_config_t ledc_channel_buzzer = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_BUZZER,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_PWM_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel_buzzer);
}

void app_main(void) {
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    init_gpio();
    init_pwm();

    update_binary_leds(g_counter);
    update_pwm(g_counter);

    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
}