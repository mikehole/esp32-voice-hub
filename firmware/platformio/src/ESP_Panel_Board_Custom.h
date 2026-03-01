/**
 * ESP_Panel Board Configuration
 * Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 * 
 * Display: ST77916 360x360 QSPI
 * Touch: CST816 I2C
 */

#ifndef ESP_PANEL_BOARD_CUSTOM_H
#define ESP_PANEL_BOARD_CUSTOM_H

/* Set to 1 to use custom board */
#define ESP_PANEL_USE_CUSTOM_BOARD 1

/*------------------------------------------------------------------------------
 * LCD Configuration
 *----------------------------------------------------------------------------*/
#define ESP_PANEL_USE_LCD 1

/* LCD Controller: ST77916 */
#define ESP_PANEL_LCD_NAME              ST77916

/* LCD Resolution */
#define ESP_PANEL_LCD_WIDTH             360
#define ESP_PANEL_LCD_HEIGHT            360

/* LCD Bus Type: QSPI */
#define ESP_PANEL_LCD_BUS_TYPE          ESP_PANEL_BUS_TYPE_QSPI

/* QSPI Pins */
#define ESP_PANEL_LCD_BUS_HOST_ID       1
#define ESP_PANEL_LCD_BUS_SKIP_INIT_HOST 0

#define ESP_PANEL_LCD_BUS_QSPI_IO_CS     14
#define ESP_PANEL_LCD_BUS_QSPI_IO_SCK    13
#define ESP_PANEL_LCD_BUS_QSPI_IO_DATA0  15
#define ESP_PANEL_LCD_BUS_QSPI_IO_DATA1  16
#define ESP_PANEL_LCD_BUS_QSPI_IO_DATA2  17
#define ESP_PANEL_LCD_BUS_QSPI_IO_DATA3  18

/* QSPI Clock */
#define ESP_PANEL_LCD_BUS_QSPI_CLK_HZ    (40 * 1000 * 1000)

/* LCD Reset Pin */
#define ESP_PANEL_LCD_IO_RST             21

/* LCD Color Settings */
#define ESP_PANEL_LCD_COLOR_BITS         16
#define ESP_PANEL_LCD_COLOR_BGR_ORDER    0
#define ESP_PANEL_LCD_SWAP_XY            0
#define ESP_PANEL_LCD_MIRROR_X           0
#define ESP_PANEL_LCD_MIRROR_Y           0

/*------------------------------------------------------------------------------
 * LCD Backlight Configuration
 *----------------------------------------------------------------------------*/
#define ESP_PANEL_USE_BL 1
#define ESP_PANEL_BL_IO  47
#define ESP_PANEL_BL_ON_LEVEL 1
#define ESP_PANEL_BL_USE_PWM 0

/*------------------------------------------------------------------------------
 * Touch Configuration
 *----------------------------------------------------------------------------*/
#define ESP_PANEL_USE_TOUCH 1

/* Touch Controller: CST816 */
#define ESP_PANEL_TOUCH_NAME            CST816

/* Touch I2C Pins */
#define ESP_PANEL_TOUCH_BUS_HOST_ID     0
#define ESP_PANEL_TOUCH_BUS_SKIP_INIT_HOST 0

#define ESP_PANEL_TOUCH_BUS_I2C_IO_SDA  5
#define ESP_PANEL_TOUCH_BUS_I2C_IO_SCL  4
#define ESP_PANEL_TOUCH_BUS_I2C_CLK_HZ  (400 * 1000)

/* Touch I2C Address */
#define ESP_PANEL_TOUCH_I2C_ADDRESS     0x15

/* Touch Other Pins */
#define ESP_PANEL_TOUCH_IO_RST          7
#define ESP_PANEL_TOUCH_IO_INT          6

/* Touch Resolution (same as LCD) */
#define ESP_PANEL_TOUCH_H_RES           360
#define ESP_PANEL_TOUCH_V_RES           360

/* Touch Swap/Mirror */
#define ESP_PANEL_TOUCH_SWAP_XY         0
#define ESP_PANEL_TOUCH_MIRROR_X        0
#define ESP_PANEL_TOUCH_MIRROR_Y        0

#endif /* ESP_PANEL_BOARD_CUSTOM_H */
