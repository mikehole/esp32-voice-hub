/**
 * ESP32 Voice Hub - Hello World
 * Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 * 
 * Display: ST77916 360x360 round LCD (QSPI)
 * Touch: CST816 (I2C)
 */

#include <Arduino.h>
#include <lvgl.h>
#include <ESP_Panel_Library.h>

// Display dimensions
#define DISPLAY_WIDTH  360
#define DISPLAY_HEIGHT 360

// LVGL display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

// Display panel
ESP_Panel *panel = nullptr;

// LVGL display flush callback
void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    if (panel && panel->getLcd()) {
        panel->getLcd()->drawBitmap(area->x1, area->y1, w, h, (uint8_t *)color_p);
    }
    
    lv_disp_flush_ready(disp);
}

// Touch read callback
void touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    if (panel && panel->getTouch()) {
        ESP_PanelTouchPoint point;
        int touch_num = panel->getTouch()->readPoints(&point, 1);
        
        if (touch_num > 0) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = point.x;
            data->point.y = point.y;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== ESP32 Voice Hub ===");
    Serial.println("Initializing display...");
    
    // Initialize the panel (display + touch)
    panel = new ESP_Panel();
    panel->init();
    panel->begin();
    
    // Turn on backlight
    if (panel->getBacklight()) {
        panel->getBacklight()->on();
        panel->getBacklight()->setBrightness(100);
    }
    
    Serial.println("Display initialized!");
    
    // Initialize LVGL
    lv_init();
    
    // Allocate draw buffers in PSRAM
    size_t buf_size = DISPLAY_WIDTH * 40 * sizeof(lv_color_t);  // 40 lines
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    
    if (!buf1 || !buf2) {
        Serial.println("ERROR: Failed to allocate LVGL buffers!");
        return;
    }
    
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISPLAY_WIDTH * 40);
    
    // Initialize display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // Initialize touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
    
    Serial.println("LVGL initialized!");
    
    // Create the Hello World UI
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F2744), LV_PART_MAIN);  // Deep navy
    
    // Create a label
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello World!");
    lv_obj_set_style_text_color(label, lv_color_hex(0x5DADE2), LV_PART_MAIN);  // Cyan
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_center(label);
    
    // Create a smaller subtitle
    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "ESP32 Voice Hub");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x85C1E9), LV_PART_MAIN);  // Light blue
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 40);
    
    Serial.println("UI created! You should see 'Hello World!' on the display.");
}

void loop() {
    lv_timer_handler();  // Run LVGL
    delay(5);
}
