/**
 * SH8601 AMOLED Driver - Direct SPI implementation for ESP-IDF 4.4
 * Bypasses esp_lcd panel framework to support QSPI manually
 */

#ifndef ESP_LCD_SH8601_H
#define ESP_LCD_SH8601_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD initialization command structure
 */
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_bytes;
    uint16_t delay_ms;
} sh8601_lcd_init_cmd_t;

/**
 * @brief SH8601 handle
 */
typedef struct {
    spi_device_handle_t spi;
    int rst_gpio;
    int cs_gpio;
} sh8601_handle_t;

/**
 * @brief Initialize SH8601 display
 */
esp_err_t sh8601_init(sh8601_handle_t *handle, spi_host_device_t host, int cs, int rst,
                       int clk, int d0, int d1, int d2, int d3,
                       const sh8601_lcd_init_cmd_t *init_cmds, size_t init_cmds_size);

/**
 * @brief Draw bitmap to display
 */
esp_err_t sh8601_draw_bitmap(sh8601_handle_t *handle, int x_start, int y_start, 
                              int x_end, int y_end, const void *color_data);

/**
 * @brief Flush callback for LVGL
 */
typedef void (*sh8601_flush_done_cb_t)(void *user_data);

/**
 * @brief Set flush done callback
 */
void sh8601_set_flush_cb(sh8601_handle_t *handle, sh8601_flush_done_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // ESP_LCD_SH8601_H
