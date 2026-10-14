#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define LED_GPIO      GPIO_NUM_2
#define BUTTON_GPIO   GPIO_NUM_4

#define DEBOUNCE_TIME_US    50000
#define LONG_PRESS_TIME_US  2000000
#define LED_TIMEOUT_US      30000000

void app_main(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 1);

    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLDOWN_ONLY);

    int current_button_reading;
    int last_button_reading = 0;
    int stable_button_state = 0;
    int64_t last_debounce_time = 0;

    bool is_pressing = false;
    bool long_press_handled = false;
    int64_t button_press_start_time = 0;

    bool led_is_on = false;
    int64_t led_turn_on_time = 0;

    while (1) {
        int64_t current_time = esp_timer_get_time();
        
        current_button_reading = gpio_get_level(BUTTON_GPIO);

        if (current_button_reading != last_button_reading) {
            last_debounce_time = current_time;
        }

        if ((current_time - last_debounce_time) > DEBOUNCE_TIME_US) {
            if (current_button_reading != stable_button_state) {
                stable_button_state = current_button_reading;

                if (stable_button_state == 1) {
                    is_pressing = true;
                    button_press_start_time = current_time;
                    long_press_handled = false;

                    led_is_on = true;
                    led_turn_on_time = current_time;
                    gpio_set_level(LED_GPIO, 0);
                }
                else {
                    is_pressing = false;
                }
            }
        }
        last_button_reading = current_button_reading;

        if (is_pressing && !long_press_handled) {
            if ((current_time - button_press_start_time) > LONG_PRESS_TIME_US) {
                led_is_on = false;
                gpio_set_level(LED_GPIO, 1);
                long_press_handled = true;
            }
        }

        if (led_is_on) {
            if ((current_time - led_turn_on_time) > LED_TIMEOUT_US) {
                led_is_on = false;
                gpio_set_level(LED_GPIO, 1);
            }
        }

        taskYIELD();
    }
}