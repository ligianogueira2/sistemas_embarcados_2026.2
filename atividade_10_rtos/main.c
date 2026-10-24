#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define POT_ADC_CHANNEL    ADC_CHANNEL_6     
#define LED_GPIO           GPIO_NUM_25      
#define BUTTON_GPIO        GPIO_NUM_26       
#define I2C_SDA_GPIO       GPIO_NUM_8        
#define I2C_SCL_GPIO       GPIO_NUM_9      

#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000            
#define MPU6050_ADDR       0x68            
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_ACCEL_XOUT 0x3B

#define LEDC_MODE          LEDC_LOW_SPEED_MODE
#define LEDC_TIMER         LEDC_TIMER_0
#define LEDC_CHANNEL       LEDC_CHANNEL_0
#define LEDC_DUTY_RES      LEDC_TIMER_13_BIT 
#define LEDC_FREQUENCY     5000              

static const char *TAG = "RTOS_SYSTEM";

typedef struct {
    float accel_x;
    float accel_y;
    float accel_z;
    int pot_raw;
    uint32_t pot_mv;
    uint8_t led_percentage;
    bool is_hold;
} system_telemetry_t;

static QueueHandle_t xPotQueue = NULL;
static SemaphoreHandle_t xHoldSemaphore = NULL;
static SemaphoreHandle_t xSystemMutex = NULL;

static system_telemetry_t g_system_data = {0};

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static void mpu6050_init(void) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, MPU6050_PWR_MGMT_1, true);
    i2c_master_write_byte(cmd, 0x00, true); // Acordar o MPU-6050
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
}


// Task 1: Botao (Tratamento de Debounce e Sinalizacao via Semáforo Binario)
void vTaskButton(void *pvParameters) {
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    int last_state = 1;

    while (1) {
        int current_state = gpio_get_level(BUTTON_GPIO);
        if (last_state == 1 && current_state == 0) { 
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                xSemaphoreGive(xHoldSemaphore); 
            }
        }
        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void vTaskADC(void *pvParameters) {
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12, 
    };
    adc_oneshot_config_channel(adc_handle, POT_ADC_CHANNEL, &chan_config);

    int raw_adc = 0;

    while (1) {
        if (adc_oneshot_read(adc_handle, POT_ADC_CHANNEL, &raw_adc) == ESP_OK) {
            xQueueSend(xPotQueue, &raw_adc, pdMS_TO_TICKS(10));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void vTaskLED(void *pvParameters) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LED_GPIO,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ch_cfg);

    int received_adc = 0;
    bool is_hold_mode = false;
    uint32_t duty_13bits = 0;

    while (1) {
        if (xSemaphoreTake(xHoldSemaphore, 0) == pdTRUE) {
            is_hold_mode = !is_hold_mode;
        }

        if (xQueueReceive(xPotQueue, &received_adc, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!is_hold_mode) {
                duty_13bits = (uint32_t)(received_adc << 1);
                if (duty_13bits > 8191) duty_13bits = 8191;

                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty_13bits);
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            }
        }

        if (xSemaphoreTake(xSystemMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            g_system_data.is_hold = is_hold_mode;
            if (!is_hold_mode) {
                g_system_data.pot_raw = received_adc;
                g_system_data.pot_mv = (uint32_t)((received_adc * 3300) / 4095);
                g_system_data.led_percentage = (uint8_t)((duty_13bits * 100) / 8191);
            }
            xSemaphoreGive(xSystemMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void vTaskMPU6050(void *pvParameters) {
    uint8_t raw_data[6];

    while (1) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, MPU6050_ACCEL_XOUT, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_READ, true);
        i2c_master_read(cmd, raw_data, 6, I2C_MASTER_LAST_NACK);
        i2c_master_stop(cmd);

        if (i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100)) == ESP_OK) {
            int16_t raw_x = (int16_t)((raw_data[0] << 8) | raw_data[1]);
            int16_t raw_y = (int16_t)((raw_data[2] << 8) | raw_data[3]);
            int16_t raw_z = (int16_t)((raw_data[4] << 8) | raw_data[5]);

            // Atualiza aceleração na estrutura partilhada com protecao por Mutex
            if (xSemaphoreTake(xSystemMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                g_system_data.accel_x = raw_x / 16384.0f;
                g_system_data.accel_y = raw_y / 16384.0f;
                g_system_data.accel_z = raw_z / 16384.0f;
                xSemaphoreGive(xSystemMutex);
            }
        }
        i2c_cmd_link_delete(cmd);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskTelemetry(void *pvParameters) {
    TickType_xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(500);
    xLastWakeTime = xTaskGetTickCount();

    system_telemetry_t temp_data;

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        if (xSemaphoreTake(xSystemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            temp_data = g_system_data;
            xSemaphoreGive(xSystemMutex);

            printf("=======================\n");
            printf("STATUS: [%s]\n", temp_data.is_hold ? "HOLD" : "LIVE");
            printf("POT: %d (%lu mV) | LED: %d%%\n",
                   temp_data.pot_raw,
                   (unsigned long)temp_data.pot_mv,
                   temp_data.led_percentage);
            printf("IMU ACCEL (g):\n");
            printf("X: %.2f | Y: %.2f | Z: %.2f\n",
                   temp_data.accel_x,
                   temp_data.accel_y,
                   temp_data.accel_z);
            printf("=======================\n\n");
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "A inicializar o sistema multitarefa com FreeRTOS...");

    xPotQueue = xQueueCreate(5, sizeof(int));
    xHoldSemaphore = xSemaphoreCreateBinary();
    xSystemMutex = xSemaphoreCreateMutex();

    // Inicializacao do Barramento I2C e MPU-6050
    ESP_ERROR_CHECK(i2c_master_init());
    mpu6050_init();

    xTaskCreate(vTaskButton,    "Task_Button",    2048, NULL, 4, NULL);
    xTaskCreate(vTaskLED,       "Task_LED",       2048, NULL, 3, NULL);
    xTaskCreate(vTaskADC,       "Task_ADC",       2048, NULL, 2, NULL);
    xTaskCreate(vTaskMPU6050,   "Task_MPU6050",   2048, NULL, 2, NULL);
    xTaskCreate(vTaskTelemetry, "Task_Telemetry", 3072, NULL, 1, NULL);
}