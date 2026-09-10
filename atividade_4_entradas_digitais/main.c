#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BTN_A_PIN GPIO_NUM_4   
#define BTN_B_PIN GPIO_NUM_5  

#define LED_0_PIN GPIO_NUM_2   
#define LED_1_PIN GPIO_NUM_42  
#define LED_2_PIN GPIO_NUM_40 
#define LED_3_PIN GPIO_NUM_39 

#define DEBOUNCE_TIME_MS 50 

static const char *TAG = "CONTADOR_4BITS";

static const gpio_num_t led_pins[4] = {LED_0_PIN, LED_1_PIN, LED_2_PIN, LED_3_PIN};

static void init_hardware(void) {
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << LED_0_PIN) | (1ULL << LED_1_PIN) | 
                        (1ULL << LED_2_PIN) | (1ULL << LED_3_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_led);

    gpio_config_t io_conf_btn = {
        .pin_bit_mask = (1ULL << BTN_A_PIN) | (1ULL << BTN_B_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_btn);
}

static void update_leds(uint8_t count) {
    for (int i = 0; i < 4; i++) {
        int bit_val = (count >> i) & 0x01;
        gpio_set_level(led_pins[i], bit_val);
    }
}

 // Função de verificação de clique por estabilização de estado (Debounce por Software)

static bool is_button_pressed(gpio_num_t pin, int *last_state, int64_t *last_time) {
    int current_reading = gpio_get_level(pin);
    int64_t now = esp_timer_get_time() / 1000;

    if (current_reading != *last_state) {
        *last_time = now;
        *last_state = current_reading;
    }

    // Se o sinal ficou estável pelo tempo de debounce e o botão está pressionado (0)
    if ((now - *last_time) >= DEBOUNCE_TIME_MS) {
        if (current_reading == 0) {
            return true;
        }
    }
    return false;
}

void app_main(void) {
    init_hardware();

    uint8_t counter = 0;
    uint8_t increment_step = 1;

    int last_state_A = 1, last_state_B = 1;
    int64_t time_A = 0, time_B = 0;
    bool handled_A = false, handled_B = false;

    update_leds(counter);
    ESP_LOGI(TAG, "Sistema Inicializado! Contador: %d | Passo: +%d", counter, increment_step);

    while (1) {
        // Botão A (incremento)
        if (is_button_pressed(BTN_A_PIN, &last_state_A, &time_A)) {
            if (!handled_A) {
                counter = (counter + increment_step) % 16;
                update_leds(counter);
                ESP_LOGI(TAG, "Botao A Pressionado -> Novo Valor: %d (0x%X) | Passo: +%d", counter, counter, increment_step);
                handled_A = true;
            }
        } else {
            handled_A = false;
        }

        // Botão B (+1 / +2)
        if (is_button_pressed(BTN_B_PIN, &last_state_B, &time_B)) {
            if (!handled_B) {
                increment_step = (increment_step == 1) ? 2 : 1;
                ESP_LOGI(TAG, "Botao B Pressionado -> Passo de Incremento Alterado para: +%d", increment_step);
                handled_B = true;
            }
        } else {
            handled_B = false; 
        }

        // Delay de 10ms para permitir execução correta do FreeRTOS
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}