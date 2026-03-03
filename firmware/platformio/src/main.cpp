/**
 * ESP32 Voice Hub - Main Application
 * Using Waveshare native drivers for SH8601 AMOLED
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
    Serial.println("Touch initialized");
    
    // Initialize LCD with LVGL
    lcd_lvgl_Init();
    Serial.println("LCD initialized");
    
    // Initialize backlight
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    Serial.println("Backlight initialized");
    
    // Set black background (true black on AMOLED = pixels off)
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    
    // Create a simple test label
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "ESP32 Voice Hub\nDisplay OK!");
    lv_obj_set_style_text_color(label, lv_color_hex(0x5DADE2), 0);  // Cyan
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    
    Serial.println("Setup complete!");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
