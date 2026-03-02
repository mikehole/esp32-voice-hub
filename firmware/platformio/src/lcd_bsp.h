/**
 * LCD BSP Header - Simplified for ESP-IDF 4.4 / Arduino
 */

#ifndef LCD_BSP_H
#define LCD_BSP_H

#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

void lcd_lvgl_Init(void);

#ifdef __cplusplus
}
#endif

#endif // LCD_BSP_H
