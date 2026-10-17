#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#define POT_ADC_CHANNEL     ADC_CHANNEL_3     
#define LED_PWM_GPIO        GPIO_NUM_2         
#define BTN_HOLD_GPIO       GPIO_NUM_5         

#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT  // Resolução: 0 a 1023
#define LEDC_FREQUENCY      5000               // Frequência: 5 kHz

typedef enum {
    MODE_LIVE = 0,
    MODE_HOLD
} system_mode_t;

static const char *TAG = "ATIVIDADE_09";

// Função para debounce (não-bloqueante)
static bool check_button_pressed(void) {
    static uint32_t last_debounce_time = 0;
    static int last_btn_state = 1;
    
    int current_state = gpio_get_level(BTN_HOLD_GPIO);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (current_state != last_btn_state) {
        last_debounce_time = now;
        last_btn_state = current_state;
    }

    if ((now - last_debounce_time) > 50) { 
        static int debounced_state = 1;
        if (current_state != debounced_state) {
            debounced_state = current_state;
            if (debounced_state == 0) { 
                return true;
            }
        }
    }
    return false;
}

void app_main(void)
{
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,           // 12 bits: 0 a 4095
        .atten = ADC_ATTEN_DB_12,             
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, POT_ADC_CHANNEL, &config));

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_PWM_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << BTN_HOLD_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_config);

    system_mode_t current_mode = MODE_LIVE;
    int adc_raw = 0;
    uint32_t voltage_mv = 0;
    uint32_t pwm_duty = 0;
    
    TickType_t last_telemetry_time = xTaskGetTickCount();

    while (1) {
        if (check_button_pressed()) {
            current_mode = (current_mode == MODE_LIVE) ? MODE_HOLD : MODE_LIVE;
            ESP_LOGW(TAG, "Modo alterado para: %s", (current_mode == MODE_LIVE) ? "LIVE" : "HOLD");
        }

        if (current_mode == MODE_LIVE) {
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, POT_ADC_CHANNEL, &adc_raw));

            voltage_mv = (adc_raw * 3300) / 4095;

            pwm_duty = (adc_raw * 1023) / 4095;

            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, pwm_duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_telemetry_time) >= pdMS_TO_TICKS(500)) {
            last_telemetry_time = now;
            
            ESP_LOGI(TAG, "ADC Raw: %4d | Tensão: %4lu mV | Modo: %s", 
                     adc_raw, 
                     voltage_mv, 
                     (current_mode == MODE_LIVE) ? "LIVE" : "HOLD");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}