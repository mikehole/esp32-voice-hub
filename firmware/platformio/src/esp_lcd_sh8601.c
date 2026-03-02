/**
 * SH8601 AMOLED Driver - Direct QSPI implementation for ESP-IDF 4.4
 */

#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sh8601";

static sh8601_flush_done_cb_t s_flush_cb = NULL;
static void *s_flush_user_data = NULL;

// Pre-transaction callback to handle QSPI command phase
static void IRAM_ATTR spi_pre_transfer_callback(spi_transaction_t *t) {
    // Nothing needed - using command phase in transaction
}

// Post-transaction callback
static void IRAM_ATTR spi_post_transfer_callback(spi_transaction_t *t) {
    if (t->user && s_flush_cb) {
        s_flush_cb(s_flush_user_data);
    }
}

esp_err_t sh8601_init(sh8601_handle_t *handle, spi_host_device_t host, int cs, int rst,
                       int clk, int d0, int d1, int d2, int d3,
                       const sh8601_lcd_init_cmd_t *init_cmds, size_t init_cmds_size)
{
    esp_err_t ret;
    
    handle->rst_gpio = rst;
    handle->cs_gpio = cs;
    
    // Initialize SPI bus with QSPI pins
    spi_bus_config_t buscfg = {
        .sclk_io_num = clk,
        .mosi_io_num = d0,        // DATA0 as MOSI for init commands
        .miso_io_num = -1,
        .quadwp_io_num = d2,      // DATA2 as WP for quad mode
        .quadhd_io_num = d3,      // DATA3 as HD for quad mode
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 360 * 360 * 2 + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,
    };
    
    ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %d", ret);
        return ret;
    }
    
    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,  // 40 MHz
        .mode = 0,
        .spics_io_num = cs,
        .queue_size = 10,
        .pre_cb = spi_pre_transfer_callback,
        .post_cb = spi_post_transfer_callback,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .command_bits = 32,  // For QSPI: 0x32 + 24-bit address
        .address_bits = 0,
    };
    
    ret = spi_bus_add_device(host, &devcfg, &handle->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %d", ret);
        return ret;
    }
    
    // Hardware reset
    if (rst >= 0) {
        gpio_set_direction(rst, GPIO_MODE_OUTPUT);
        gpio_set_level(rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    
    // Send initialization commands
    // Use standard SPI for init (not quad mode)
    for (size_t i = 0; i < init_cmds_size; i++) {
        // Build command: 0x02 (write) + cmd + data
        uint8_t tx_buf[20];
        tx_buf[0] = 0x02;  // Write command for single-line SPI
        tx_buf[1] = 0x00;  // High address byte
        tx_buf[2] = init_cmds[i].cmd;  // Register
        tx_buf[3] = 0x00;  // Low address byte
        memcpy(&tx_buf[4], init_cmds[i].data, init_cmds[i].data_bytes);
        
        spi_transaction_t t = {
            .length = (4 + init_cmds[i].data_bytes) * 8,
            .tx_buffer = tx_buf,
            .user = NULL,
        };
        
        ret = spi_device_transmit(handle->spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Init cmd 0x%02x failed", init_cmds[i].cmd);
        }
        
        if (init_cmds[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
        }
    }
    
    ESP_LOGI(TAG, "SH8601 initialized");
    return ESP_OK;
}

esp_err_t sh8601_draw_bitmap(sh8601_handle_t *handle, int x_start, int y_start, 
                              int x_end, int y_end, const void *color_data)
{
    esp_err_t ret;
    
    // Set column address (0x2A)
    uint8_t col_cmd[] = {
        0x02, 0x00, 0x2A, 0x00,  // Write cmd + address
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF
    };
    spi_transaction_t t_col = {
        .length = sizeof(col_cmd) * 8,
        .tx_buffer = col_cmd,
        .user = NULL,
    };
    ret = spi_device_transmit(handle->spi, &t_col);
    
    // Set row address (0x2B)
    uint8_t row_cmd[] = {
        0x02, 0x00, 0x2B, 0x00,
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF
    };
    spi_transaction_t t_row = {
        .length = sizeof(row_cmd) * 8,
        .tx_buffer = row_cmd,
        .user = NULL,
    };
    ret = spi_device_transmit(handle->spi, &t_row);
    
    // Write memory command (0x2C) - using quad mode for pixel data
    // First send the command header
    uint8_t write_cmd[] = {0x32, 0x00, 0x2C, 0x00};  // 0x32 = quad write
    spi_transaction_t t_cmd = {
        .length = sizeof(write_cmd) * 8,
        .tx_buffer = write_cmd,
        .user = NULL,
    };
    ret = spi_device_transmit(handle->spi, &t_cmd);
    
    // Now send pixel data in quad mode
    size_t len = (x_end - x_start) * (y_end - y_start) * 2;
    spi_transaction_t t_data = {
        .length = len * 8,
        .tx_buffer = color_data,
        .user = (void*)1,  // Mark as color data for callback
        .flags = SPI_TRANS_MODE_QIO,
    };
    ret = spi_device_transmit(handle->spi, &t_data);
    
    return ret;
}

void sh8601_set_flush_cb(sh8601_handle_t *handle, sh8601_flush_done_cb_t cb, void *user_data)
{
    s_flush_cb = cb;
    s_flush_user_data = user_data;
}
