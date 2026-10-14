#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"

#define LED_PIN       GPIO_NUM_12
#define BUTTON_PIN    GPIO_NUM_4

#define DEBOUNCE_TIME_US    (200 * 1000)       
#define TIMEOUT_LED_US      (10 * 1000 * 1000) 
#define LONG_PRESS_TIME_US  (2 * 1000 * 1000)  

static esp_timer_handle_t led_timer_handle = NULL;
static esp_timer_handle_t long_press_timer_handle = NULL;
static volatile int64_t last_interrupt_time = 0;

static void led_timer_callback(void* arg) {
    gpio_set_level(LED_PIN, 0);
}

static void long_press_timer_callback(void* arg) {
    if (gpio_get_level(BUTTON_PIN) == 1) {
        gpio_set_level(LED_PIN, 0);        
        esp_timer_stop(led_timer_handle);  
    }
}

// ISR: Disparada ao pressionar o botão (Borda de Subida)
static void IRAM_ATTR button_isr_handler(void* arg) {
    int64_t current_time = esp_timer_get_time();

    if ((current_time - last_interrupt_time) < DEBOUNCE_TIME_US) {
        return;
    }
    last_interrupt_time = current_time;

    gpio_set_level(LED_PIN, 1);

    esp_timer_stop(led_timer_handle);
    esp_timer_start_once(led_timer_handle, TIMEOUT_LED_US);

    esp_timer_stop(long_press_timer_handle);
    esp_timer_start_once(long_press_timer_handle, LONG_PRESS_TIME_US);
}

void app_main(void) {
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_led);
    gpio_set_level(LED_PIN, 0);

    gpio_config_t io_conf_button = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_POSEDGE         
    };
    gpio_config(&io_conf_button);

    // Criação dos Timers
    const esp_timer_create_args_t led_timer_args = {
        .callback = &led_timer_callback,
        .name = "led_timeout"
    };
    esp_timer_create(&led_timer_args, &led_timer_handle);

    const esp_timer_create_args_t long_press_args = {
        .callback = &long_press_timer_callback,
        .name = "long_press"
    };
    esp_timer_create(&long_press_args, &long_press_timer_handle);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}