/**
 * Touch Input Module - CST816 capacitive touch
 */

#include "touch.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "touch";

#define I2C_PORT        I2C_NUM_0
#define TOUCH_ADDR      0x15
#define TOUCH_SDA_PIN   GPIO_NUM_11
#define TOUCH_SCL_PIN   GPIO_NUM_12

static bool initialized = false;

void touch_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_PIN,
        .scl_io_num = TOUCH_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 300000,
    };
    
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return;
    }
    
    err = i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C install failed: %s", esp_err_to_name(err));
        return;
    }
    
    // Switch to normal mode
    uint8_t data[2] = {0x00, 0x00};
    i2c_master_write_to_device(I2C_PORT, TOUCH_ADDR, data, 2, pdMS_TO_TICKS(100));
    
    initialized = true;
    ESP_LOGI(TAG, "Touch initialized");
}

bool touch_read(uint16_t *x, uint16_t *y)
{
    if (!initialized) return false;
    
    uint8_t reg = 0x00;
    uint8_t data[7] = {0};
    
    esp_err_t err = i2c_master_write_read_device(I2C_PORT, TOUCH_ADDR, &reg, 1, data, 7, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        return false;
    }
    
    uint8_t num_points = data[2];
    if (num_points > 0) {
        *x = ((uint16_t)(data[3] & 0x0f) << 8) + (uint16_t)data[4];
        *y = ((uint16_t)(data[5] & 0x0f) << 8) + (uint16_t)data[6];
        return true;
    }
    
    return false;
}

bool touch_is_pressed(void)
{
    uint16_t x, y;
    return touch_read(&x, &y);
}
