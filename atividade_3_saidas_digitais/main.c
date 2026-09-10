#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED1_GPIO GPIO_NUM_2  // Azul
#define LED2_GPIO GPIO_NUM_42 // Vermelho
#define LED3_GPIO GPIO_NUM_40 // Amarelo
#define LED4_GPIO GPIO_NUM_39 // Verde

#define DELAY_MS 500

const gpio_num_t leds[4] = {LED1_GPIO, LED2_GPIO, LED3_GPIO, LED4_GPIO};


void configure_leds(void) {
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(leds[i]);
        gpio_set_direction(leds[i], GPIO_MODE_OUTPUT);
        gpio_set_level(leds[i], 0);
    }
}


void set_led_state(uint8_t led_index, uint8_t state) {
    if (led_index < 4) {
        gpio_set_level(leds[led_index], state ? 1 : 0);
    }
}


void turn_off_all_leds(void) {
    for (int i = 0; i < 4; i++) {
        set_led_state(i, 0);
    }
}


// Fase 1: contador binário (0 a 15 / 0000 até 1111)

void fase1_contador_binario(void) {
    for (int i = 0; i < 16; i++) {
        for (int bit = 0; bit < 4; bit++) {
            uint8_t state = (i >> bit) & 0x01;
            set_led_state(bit, state);
        }
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }
}

// Fase 2: acende os LEDs sequencialmente e depois retorna

void fase2_varredura(void) {
    turn_off_all_leds();

    for (int i = 0; i < 4; i++) {
        set_led_state(i, 1);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
        set_led_state(i, 0);
    }

    for (int i = 3; i >= 0; i--) {
        set_led_state(i, 1);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
        set_led_state(i, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
}


void app_main(void) {
    configure_leds();

    while (1) {
        fase1_contador_binario();
        fase2_varredura();
    }
}