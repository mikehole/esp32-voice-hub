/**
 * ESP_Panel Configuration for Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 * ST77916 QSPI Display + CST816 Touch
 */

#ifndef ESP_PANEL_CONF_H
#define ESP_PANEL_CONF_H

/* Set to 1 to enable board config */
#define ESP_PANEL_USE_BOARD     0

/* LCD Settings */
#define ESP_PANEL_USE_LCD       1
#define ESP_PANEL_LCD_WIDTH     360
#define ESP_PANEL_LCD_HEIGHT    360

/* Backlight */
#define ESP_PANEL_USE_BACKLIGHT 1
#define ESP_PANEL_BACKLIGHT_IO  47
#define ESP_PANEL_BACKLIGHT_ON_LEVEL 1

/* Touch Settings */
#define ESP_PANEL_USE_TOUCH     1

#endif /* ESP_PANEL_CONF_H */
