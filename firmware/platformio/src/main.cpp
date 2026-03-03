/**
 * ESP32 Voice Hub - Main Application
 * Radial Wedge UI with 8 segments (Trivial Pursuit style)
 */

#include <Arduino.h>
#include <math.h>
#include "lcd_bsp.h"
#include "cst816.h"
#include "lcd_bl_pwm_bsp.h"
#include "lcd_config.h"

// Color palette - Blue Mono design
#define COLOR_BG           lv_color_hex(0x000000)  // True black
#define COLOR_WEDGE        lv_color_hex(0x0F2744)  // Deep navy
#define COLOR_WEDGE_ALT    lv_color_hex(0x1A3A5C)  // Slightly lighter navy
#define COLOR_SELECTED     lv_color_hex(0x5DADE2)  // Cyan highlight
#define COLOR_CENTER       lv_color_hex(0x0A1929)  // Dark center
#define COLOR_TEXT         lv_color_hex(0x5DADE2)  // Cyan text
#define COLOR_BORDER       lv_color_hex(0x2E86AB)  // Border blue

// Display dimensions
#define SCREEN_SIZE     360
#define CENTER_X        (SCREEN_SIZE / 2)
#define CENTER_Y        (SCREEN_SIZE / 2)
#define OUTER_RADIUS    170
#define INNER_RADIUS    70
#define CENTER_RADIUS   60

// Wedge labels
const char* wedge_labels[] = {
    "Voice", "Music", "Home", "Weather",
    "News", "Timer", "Lights", "Settings"
};

// Draw a single wedge segment using arcs
void draw_wedge(lv_obj_t* parent, int index, bool selected) {
    int start_angle = index * 45 - 90;  // Start from top, 45° each
    int end_angle = start_angle + 44;   // Small gap between wedges
    
    // Create arc for wedge
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, OUTER_RADIUS * 2, OUTER_RADIUS * 2);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, start_angle);
    lv_arc_set_bg_angles(arc, 0, 44);
    lv_arc_set_value(arc, 0);
    
    // Style the arc
    lv_obj_set_style_arc_width(arc, OUTER_RADIUS - INNER_RADIUS, LV_PART_MAIN);
    lv_color_t wedge_color = selected ? COLOR_SELECTED : 
                             (index % 2 == 0 ? COLOR_WEDGE : COLOR_WEDGE_ALT);
    lv_obj_set_style_arc_color(arc, wedge_color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    
    // Hide the indicator and knob
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    
    // Remove arc interactivity
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    
    // Add label at wedge center
    float label_angle = (start_angle + 22) * M_PI / 180.0;
    float label_radius = (OUTER_RADIUS + INNER_RADIUS) / 2;
    int label_x = CENTER_X + (int)(cos(label_angle) * label_radius);
    int label_y = CENTER_Y + (int)(sin(label_angle) * label_radius);
    
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, wedge_labels[index]);
    lv_obj_set_style_text_color(label, selected ? COLOR_BG : COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(label, label_x - 25, label_y - 8);
}

void create_radial_ui() {
    lv_obj_t* screen = lv_scr_act();
    
    // Black background
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    
    // Draw 8 wedges (one selected for demo)
    for (int i = 0; i < 8; i++) {
        draw_wedge(screen, i, i == 0);  // First wedge selected
    }
    
    // Center circle
    lv_obj_t* center = lv_obj_create(screen);
    lv_obj_set_size(center, CENTER_RADIUS * 2, CENTER_RADIUS * 2);
    lv_obj_center(center);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center, COLOR_CENTER, 0);
    lv_obj_set_style_border_color(center, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(center, 2, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    
    // Center icon/text
    lv_obj_t* center_label = lv_label_create(center);
    lv_label_set_text(center_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(center_label, COLOR_SELECTED, 0);
    lv_obj_set_style_text_font(center_label, &lv_font_montserrat_32, 0);
    lv_obj_center(center_label);
}

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
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_200);
    Serial.println("Backlight initialized");
    
    // Create the radial wedge UI
    create_radial_ui();
    
    Serial.println("Setup complete!");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
