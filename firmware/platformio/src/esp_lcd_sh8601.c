/**
 * SH8601 AMOLED Driver - Simplified for ESP-IDF 4.4 / Arduino
 * Based on Waveshare demo code
 */

#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sh8601";

// QSPI command format: 0x32 + 24-bit address
#define SH8601_QSPI_CMD_DATA_WRITE 0x32

esp_err_t sh8601_init(esp_lcd_panel_io_handle_t io, int rst_gpio, const sh8601_lcd_init_cmd_t *init_cmds, size_t init_cmds_size)
{
    // Reset
    if (rst_gpio >= 0) {
        gpio_set_direction(rst_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // Send init commands
    for (size_t i = 0; i < init_cmds_size; i++) {
        uint32_t cmd = (SH8601_QSPI_CMD_DATA_WRITE << 24) | (init_cmds[i].cmd << 8);
        esp_lcd_panel_io_tx_param(io, cmd, init_cmds[i].data, init_cmds[i].data_bytes);
        if (init_cmds[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
        }
    }

    ESP_LOGI(TAG, "SH8601 initialized");
    return ESP_OK;
}

esp_err_t sh8601_draw_bitmap(esp_lcd_panel_io_handle_t io, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    // Set column address (0x2A)
    uint8_t col_data[] = {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF
    };
    uint32_t cmd = (SH8601_QSPI_CMD_DATA_WRITE << 24) | (0x2A << 8);
    esp_lcd_panel_io_tx_param(io, cmd, col_data, 4);

    // Set row address (0x2B)
    uint8_t row_data[] = {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF
    };
    cmd = (SH8601_QSPI_CMD_DATA_WRITE << 24) | (0x2B << 8);
    esp_lcd_panel_io_tx_param(io, cmd, row_data, 4);

    // Write memory (0x2C)
    size_t len = (x_end - x_start) * (y_end - y_start) * 2; // 16bpp
    cmd = (SH8601_QSPI_CMD_DATA_WRITE << 24) | (0x2C << 8);
    esp_lcd_panel_io_tx_color(io, cmd, color_data, len);

    return ESP_OK;
}
