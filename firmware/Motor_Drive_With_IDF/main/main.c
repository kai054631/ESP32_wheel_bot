/*
 * Bring-up test for the new custom PCB's pin map:
 *   - TB6612FNG motor driver: PWMA=4 AIN2=5 AIN1=6 STBY=7 BIN1=15 BIN2=16 PWMB=17
 *   - Quadrature encoders:    motor A = A1=10/A2=11, motor B = B1=12/B2=13
 *   - GY-85 IMU over I2C:     SDA=9 SCL=8
 *
 * Not the final robot logic — just exercises each subsystem so you can
 * confirm it's wired and working: motors spin both ways, encoders count,
 * GY-85 chips ack and report live data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "PinTest";

// ---- TB6612FNG motor driver ----
#define PWMA 4
#define AIN2 5
#define AIN1 6
#define STBY 7
#define BIN1 15
#define BIN2 16
#define PWMB 17

// ---- Quadrature encoders ----
#define ENC_A1 10 // motor A encoder phase 1
#define ENC_A2 11 // motor A encoder phase 2
#define ENC_B1 12 // motor B encoder phase 1
#define ENC_B2 13 // motor B encoder phase 2

// ---- GY-85 (I2C) ----
#define I2C_SDA_GPIO   9
#define I2C_SCL_GPIO   8
#define I2C_PORT       I2C_NUM_0
#define I2C_FREQ_HZ    100000
#define ADXL345_ADDR   0x53
#define ITG3200_ADDR   0x68
#define HMC5883L_ADDR  0x1E

// ---- Battery voltage sense (divider: R1=100k top, R2=22k bottom) ----
// GPIO14 on ESP32-S3 is ADC2 channel 3.
#define VSENSE_ADC_UNIT    ADC_UNIT_2
#define VSENSE_ADC_CHANNEL ADC_CHANNEL_3
#define VSENSE_ATTEN       ADC_ATTEN_DB_12  // ~0-3300mV input range
#define VSENSE_DIVIDER_RATIO ((100000.0 + 22000.0) / 22000.0) // 5.545

#define MOTOR_B 0
#define MOTOR_A 1

#define PWMA_CHANNEL LEDC_CHANNEL_0
#define PWMB_CHANNEL LEDC_CHANNEL_1
#define PWM_TIMER    LEDC_TIMER_0
#define PWM_MODE     LEDC_LOW_SPEED_MODE
#define PWM_DUTY_RES LEDC_TIMER_10_BIT // 0-1023
#define PWM_FREQUENCY 25000            // above audible range

#define ENC_HIGH_LIMIT 100
#define ENC_LOW_LIMIT -100

static pcnt_unit_handle_t unitA = NULL;
static pcnt_unit_handle_t unitB = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;

// ---------------- Motor driver ----------------

static void motors_init(void)
{
    gpio_config_t dir_conf = {
        .pin_bit_mask = (1ULL << AIN1) | (1ULL << AIN2) | (1ULL << BIN1) |
                         (1ULL << BIN2) | (1ULL << STBY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&dir_conf));

    ledc_timer_config_t timer_cfg = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_DUTY_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQUENCY,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chA = {
        .gpio_num = PWMA, .speed_mode = PWM_MODE,
        .channel = PWMA_CHANNEL, .timer_sel = PWM_TIMER, .duty = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&chA));

    ledc_channel_config_t chB = {
        .gpio_num = PWMB, .speed_mode = PWM_MODE,
        .channel = PWMB_CHANNEL, .timer_sel = PWM_TIMER, .duty = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&chB));

    gpio_set_level(STBY, 1); // wake the TB6612
}

// value: -100..100. Direction pins pick CW/CCW, PWM pin sets speed.
static void motorMove(int mot, int value)
{
    if (value > 100) value = 100;
    if (value < -100) value = -100;
    int duty = (abs(value) * 1023) / 100;

    int ain1 = value > 0, ain2 = value < 0;
    if (mot == MOTOR_A) {
        gpio_set_level(AIN1, ain1);
        gpio_set_level(AIN2, ain2);
        ledc_set_duty(PWM_MODE, PWMA_CHANNEL, duty);
        ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
        ESP_LOGI(TAG, "Motor A duty=%d dir=%s", duty, value > 0 ? "fwd" : value < 0 ? "rev" : "stop");
    } else {
        gpio_set_level(BIN1, ain1);
        gpio_set_level(BIN2, ain2);
        ledc_set_duty(PWM_MODE, PWMB_CHANNEL, duty);
        ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
        ESP_LOGI(TAG, "Motor B duty=%d dir=%s", duty, value > 0 ? "fwd" : value < 0 ? "rev" : "stop");
    }
}

// ---------------- Encoders (PCNT, 4x quadrature decode) ----------------

static void encoders_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = ENC_HIGH_LIMIT,
        .low_limit = ENC_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &unitA));
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &unitB));

    pcnt_glitch_filter_config_t filter_cfg = {.max_glitch_ns = 1000};
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unitA, &filter_cfg));
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unitB, &filter_cfg));

    pcnt_chan_config_t chanA_cfg = {.edge_gpio_num = ENC_A1, .level_gpio_num = ENC_A2};
    pcnt_channel_handle_t chanA;
    ESP_ERROR_CHECK(pcnt_new_channel(unitA, &chanA_cfg, &chanA));

    pcnt_chan_config_t chanB_cfg = {.edge_gpio_num = ENC_B1, .level_gpio_num = ENC_B2};
    pcnt_channel_handle_t chanB;
    ESP_ERROR_CHECK(pcnt_new_channel(unitB, &chanB_cfg, &chanB));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(unitA));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unitA));
    ESP_ERROR_CHECK(pcnt_unit_start(unitA));

    ESP_ERROR_CHECK(pcnt_unit_enable(unitB));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unitB));
    ESP_ERROR_CHECK(pcnt_unit_start(unitB));
}

// ---------------- GY-85 over I2C ----------------
// Same probe pattern as firmware/GY85_Check/main/main.c (already proven working
// on this exact SDA/SCL pair) — scan + per-chip identification register check.

static esp_err_t read_regs(uint8_t dev_addr, uint8_t reg, uint8_t *out, size_t len)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev_handle;
    esp_err_t err = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
    if (err != ESP_OK) return err;
    err = i2c_master_transmit_receive(dev_handle, &reg, 1, out, len, 100);
    i2c_master_bus_rm_device(dev_handle);
    return err;
}

static void gy85_init_and_identify(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "I2C scan (SDA=%d SCL=%d):", I2C_SDA_GPIO, I2C_SCL_GPIO);
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(i2c_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  found 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "scan done — %d device(s)", found);

    uint8_t devid = 0;
    if (read_regs(ADXL345_ADDR, 0x00, &devid, 1) == ESP_OK)
        ESP_LOGI(TAG, "ADXL345 DEVID=0x%02X (expect 0xE5) -> %s", devid, devid == 0xE5 ? "MATCH" : "MISMATCH");
    else
        ESP_LOGW(TAG, "ADXL345 did not respond");

    uint8_t whoami = 0;
    if (read_regs(ITG3200_ADDR, 0x00, &whoami, 1) == ESP_OK)
        ESP_LOGI(TAG, "ITG3200 WHO_AM_I=0x%02X (expect 0x%02X) -> %s", whoami, ITG3200_ADDR,
                 whoami == ITG3200_ADDR ? "MATCH" : "MISMATCH");
    else
        ESP_LOGW(TAG, "ITG3200 did not respond");

    uint8_t id[3] = {0};
    bool hmc_ok = read_regs(HMC5883L_ADDR, 0x0A, &id[0], 1) == ESP_OK &&
                  read_regs(HMC5883L_ADDR, 0x0B, &id[1], 1) == ESP_OK &&
                  read_regs(HMC5883L_ADDR, 0x0C, &id[2], 1) == ESP_OK;
    if (hmc_ok)
        ESP_LOGI(TAG, "HMC5883L ID='%c%c%c' (expect 'H43') -> %s", id[0], id[1], id[2],
                 (id[0] == 'H' && id[1] == '4' && id[2] == '3') ? "MATCH" : "MISMATCH");
    else if (i2c_master_probe(i2c_bus, 0x0D, 50) == ESP_OK)
        ESP_LOGW(TAG, "HMC5883L silent but 0x0D acked -> likely a QMC5883L clone");
    else
        ESP_LOGW(TAG, "HMC5883L did not respond, 0x0D silent too -> mag not wired/powered");
}

// ---------------- Battery voltage sense (ADC) ----------------
// Reuses the adc_oneshot pattern from firmware/Analog_Input/main/main.c.

static adc_oneshot_unit_handle_t vsense_adc = NULL;

static void vsense_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {.unit_id = VSENSE_ADC_UNIT};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &vsense_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = VSENSE_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(vsense_adc, VSENSE_ADC_CHANNEL, &chan_cfg));
}

// Rough conversion, no efuse calibration (esp_adc/adc_cali) — good enough to
// confirm the divider is alive and in the right ballpark, not lab-accurate.
static void vsense_read_and_log(void)
{
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(vsense_adc, VSENSE_ADC_CHANNEL, &raw));
    float adc_mv = (raw * 3300.0f) / 4095.0f;
    float batt_mv = adc_mv * VSENSE_DIVIDER_RATIO;
    ESP_LOGI(TAG, "Vsense raw=%d adc=%.0fmV -> battery=%.2fV", raw, adc_mv, batt_mv / 1000.0f);
}

void app_main(void)
{
    motors_init();
    encoders_init();
    gy85_init_and_identify();
    vsense_init();

    int countA = 0, countB = 0;
    while (1) {
        ESP_LOGI(TAG, "== Motor A forward ==");
        motorMove(MOTOR_A, 40);
        vTaskDelay(pdMS_TO_TICKS(2000));
        motorMove(MOTOR_A, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "== Motor A backward ==");
        motorMove(MOTOR_A, -40);
        vTaskDelay(pdMS_TO_TICKS(2000));
        motorMove(MOTOR_A, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "== Motor B forward ==");
        motorMove(MOTOR_B, 40);
        vTaskDelay(pdMS_TO_TICKS(2000));
        motorMove(MOTOR_B, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "== Motor B backward ==");
        motorMove(MOTOR_B, -40);
        vTaskDelay(pdMS_TO_TICKS(2000));
        motorMove(MOTOR_B, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_ERROR_CHECK(pcnt_unit_get_count(unitA, &countA));
        ESP_ERROR_CHECK(pcnt_unit_get_count(unitB, &countB));
        ESP_LOGI(TAG, "encoder counts -> A: %d  B: %d", countA, countB);
        vsense_read_and_log();
    }
}
