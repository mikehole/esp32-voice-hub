/**
 * SH8601 AMOLED Driver - Simplified for ESP-IDF 4.4 / Arduino
 * Based on Waveshare demo code
 */

#ifndef ESP_LCD_SH8601_H
#define ESP_LCD_SH8601_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"

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
 * @brief SH8601 QSPI bus configuration macro
 */
#define SH8601_PANEL_BUS_QSPI_CONFIG(clk, d0, d1, d2, d3, max_transfer) \
    {                                                                   \
        .sclk_io_num = clk,                                            \
        .data0_io_num = d0,                                            \
        .data1_io_num = d1,                                            \
        .data2_io_num = d2,                                            \
        .data3_io_num = d3,                                            \
        .max_transfer_sz = max_transfer,                               \
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,   \
    }

/**
 * @brief SH8601 QSPI panel IO configuration macro
 */
#define SH8601_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx)                    \
    {                                                                   \
        .cs_gpio_num = cs,                                             \
        .dc_gpio_num = -1,                                             \
        .spi_mode = 0,                                                 \
        .pclk_hz = 40 * 1000 * 1000,                                  \
        .trans_queue_depth = 10,                                       \
        .on_color_trans_done = cb,                                     \
        .user_ctx = cb_ctx,                                            \
        .lcd_cmd_bits = 32,                                            \
        .lcd_param_bits = 8,                                           \
        .flags = {                                                     \
            .quad_mode = true,                                         \
        },                                                             \
    }

/**
 * @brief Initialize SH8601 panel
 */
esp_err_t sh8601_init(esp_lcd_panel_io_handle_t io, int rst_gpio, const sh8601_lcd_init_cmd_t *init_cmds, size_t init_cmds_size);

/**
 * @brief Draw bitmap to panel
 */
esp_err_t sh8601_draw_bitmap(esp_lcd_panel_io_handle_t io, int x_start, int y_start, int x_end, int y_end, const void *color_data);

#ifdef __cplusplus
}
#endif

#endif // ESP_LCD_SH8601_H
