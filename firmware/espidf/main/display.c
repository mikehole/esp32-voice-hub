/**
 * Display Module - SH8601 AMOLED + LVGL
 * 360x360 round AMOLED display via QSPI
 */

#include "display.h"
#include "esp_lcd_sh8601.h"
#include "wedge_ui.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"

static const char *TAG = "display";

// Pin definitions (from lcd_config.h)
#define LCD_H_RES              360
#define LCD_V_RES              360
#define LCD_BIT_PER_PIXEL      16

#define PIN_NUM_LCD_CS         14
#define PIN_NUM_LCD_PCLK       13
#define PIN_NUM_LCD_DATA0      15
#define PIN_NUM_LCD_DATA1      16
#define PIN_NUM_LCD_DATA2      17
#define PIN_NUM_LCD_DATA3      18
#define PIN_NUM_LCD_RST        21
#define PIN_NUM_BK_LIGHT       47

#define LCD_HOST               SPI2_HOST
#define LVGL_BUF_HEIGHT        (LCD_V_RES / 20)
#define LVGL_TICK_PERIOD_MS    2
#define LVGL_TASK_STACK_SIZE   (6 * 1024)
#define LVGL_TASK_PRIORITY     2

// LVGL mutex for thread safety
// Exported for wedge_ui.c
SemaphoreHandle_t lvgl_mutex = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_drv_t disp_drv;

// Current state
static display_state_t current_state = DISPLAY_STATE_IDLE;

// UI elements
static lv_obj_t *status_label = NULL;
static lv_obj_t *state_arc = NULL;

// SH8601 initialization commands
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    // MADCTL: 0xC0 = mirror X and Y (display appears upside-down)
    {0x36, (uint8_t[]){0xC0}, 1, 0},
};

// Backlight PWM setup
static void backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_NUM_BK_LIGHT,
        .duty           = 255,  // Full brightness
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
    
    ESP_LOGI(TAG, "Backlight initialized");
}

void display_set_brightness(uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// LVGL flush callback
static bool lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(drv);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, color_map);
}

// Rounder callback - align to even coordinates for SH8601
static void lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// LVGL tick callback
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// LVGL task
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t delay_ms = 10;
    
    while (1) {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            delay_ms = lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }
        
        if (delay_ms > 50) delay_ms = 50;
        if (delay_ms < 5) delay_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// Lock LVGL mutex
bool display_lock(int timeout_ms)
{
    if (!lvgl_mutex) return false;
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mutex, ticks) == pdTRUE;
}

// Unlock LVGL mutex
void display_unlock(void)
{
    if (lvgl_mutex) {
        xSemaphoreGive(lvgl_mutex);
    }
}

// Create simple startup UI
static void create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    
    // Black background
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    
    // Status arc (circular progress indicator)
    state_arc = lv_arc_create(screen);
    lv_obj_set_size(state_arc, 340, 340);
    lv_obj_center(state_arc);
    lv_arc_set_rotation(state_arc, 270);
    lv_arc_set_bg_angles(state_arc, 0, 360);
    lv_arc_set_value(state_arc, 100);
    lv_obj_remove_style(state_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(state_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(state_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(state_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(state_arc, lv_color_hex(0x1A3A5C), LV_PART_MAIN);
    lv_obj_set_style_arc_color(state_arc, lv_color_hex(0x5DADE2), LV_PART_INDICATOR);
    
    // "Minerva" title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Minerva");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x5DADE2), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);
    
    // Status label
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 20);
    
    // IP address (will be updated later)
    lv_obj_t *ip_label = lv_label_create(screen);
    lv_label_set_text(ip_label, "ESP-IDF v5.5");
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x444444), 0);
    lv_obj_align(ip_label, LV_ALIGN_CENTER, 0, 60);
    
    ESP_LOGI(TAG, "UI created");
}

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");
    
    // Initialize backlight
    backlight_init();
    
    // Initialize SPI bus (QSPI configuration)
    spi_bus_config_t bus_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_NUM_LCD_PCLK,
        PIN_NUM_LCD_DATA0,
        PIN_NUM_LCD_DATA1,
        PIN_NUM_LCD_DATA2,
        PIN_NUM_LCD_DATA3,
        LCD_H_RES * LCD_V_RES * LCD_BIT_PER_PIXEL / 8
    );
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");
    
    // Create panel IO (QSPI configuration)
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(
        PIN_NUM_LCD_CS,
        lvgl_flush_ready,
        &disp_drv
    );
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io_handle));
    ESP_LOGI(TAG, "Panel IO created");
    
    // Create SH8601 panel
    sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_cfg, &panel_handle));
    ESP_LOGI(TAG, "SH8601 panel created");
    
    // Reset and init panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "Panel initialized");
    
    // Initialize LVGL
    lv_init();
    ESP_LOGI(TAG, "LVGL initialized");
    
    // Allocate draw buffers in PSRAM
    lv_color_t *buf1 = heap_caps_malloc(LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 && buf2);
    
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_HEIGHT);
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);
    ESP_LOGI(TAG, "Display driver registered");
    
    // Create LVGL tick timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    
    // Create mutex
    lvgl_mutex = xSemaphoreCreateMutex();
    assert(lvgl_mutex);
    
    // Create UI - use wedge UI with avatar
    if (display_lock(-1)) {
        wedge_ui_init();
        display_unlock();
    }
    
    // Start LVGL task
    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    
    ESP_LOGI(TAG, "Display initialization complete");
}

void display_set_state(display_state_t state)
{
    current_state = state;
    
    if (!display_lock(100)) return;
    
    const char *status_text = "Ready";
    avatar_state_t avatar = AVATAR_IDLE;
    
    switch (state) {
        case DISPLAY_STATE_IDLE:
            status_text = "Ready";
            avatar = AVATAR_IDLE;
            break;
        case DISPLAY_STATE_CONNECTING:
            status_text = "Connecting...";
            avatar = AVATAR_IDLE;
            break;
        case DISPLAY_STATE_LISTENING:
            status_text = "Listening...";
            avatar = AVATAR_LISTENING;
            break;
        case DISPLAY_STATE_THINKING:
            status_text = "Thinking...";
            avatar = AVATAR_THINKING;
            break;
        case DISPLAY_STATE_SPEAKING:
            status_text = "Speaking...";
            avatar = AVATAR_SPEAKING;
            break;
        case DISPLAY_STATE_ERROR:
            status_text = "Error";
            avatar = AVATAR_IDLE;
            break;
    }
    
    // Update wedge UI avatar
    wedge_ui_set_avatar_state(avatar);
    
    // Legacy status label (if exists)
    if (status_label) {
        lv_label_set_text(status_label, status_text);
    }
    
    display_unlock();
    ESP_LOGI(TAG, "Display state: %s", status_text);
}

void display_show_notification(const char *title, const char *message)
{
    ESP_LOGI(TAG, "Notification: %s - %s", title, message);
    // TODO: Show notification overlay
}

void display_loop(void)
{
    // No-op - LVGL runs in its own task
}