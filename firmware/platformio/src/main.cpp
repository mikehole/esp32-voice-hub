/**
 * ESP32 Voice Hub - Main Entry Point
 * Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 * 
 * Using Waveshare's native drivers for SH8601 QSPI display
 */

#include <Arduino.h>
#include "lcd_bsp.h"
#include "cst816.h"
#include "lcd_bl_pwm_bsp.h"
#include "lcd_config.h"

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Voice Hub - Starting...");
    
    // Initialize touch controller
    Touch_Init();
    
    // Initialize LCD with LVGL
    lcd_lvgl_Init();
    
    // Initialize backlight (full brightness)
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    
    Serial.println("Display initialized!");
}

void loop() {
    // LVGL task handler runs in its own FreeRTOS task (created by lcd_lvgl_Init)
    // Main loop can be used for other tasks
    delay(100);
}
