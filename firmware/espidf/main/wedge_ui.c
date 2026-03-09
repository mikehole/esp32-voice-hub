/**
 * Wedge UI - Radial menu with avatar center
 */

#include "wedge_ui.h"
#include "avatar_images.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "wedge_ui";

// Display dimensions
#define SCREEN_SIZE     360
#define CENTER_X        (SCREEN_SIZE / 2)
#define CENTER_Y        (SCREEN_SIZE / 2)
#define OUTER_RADIUS    180
#define INNER_RADIUS    75
#define AVATAR_SIZE     130

// Colors
#define COLOR_BG        lv_color_black()
#define COLOR_WEDGE     lv_color_hex(0x1a1a2e)
#define COLOR_WEDGE_ALT lv_color_hex(0x16213e)
#define COLOR_SELECTED  lv_color_hex(0x0f3460)
#define COLOR_CENTER    lv_color_hex(0x1a1a2e)
#define COLOR_BORDER    lv_color_hex(0x533483)
#define COLOR_TEXT      lv_color_hex(0xe94560)

// Wedge labels
static const char* wedge_labels[] = {
    LV_SYMBOL_AUDIO,     // 0: Voice (top)
    LV_SYMBOL_HOME,      // 1: Home
    LV_SYMBOL_SETTINGS,  // 2: Settings  
    LV_SYMBOL_WIFI,      // 3: WiFi
    LV_SYMBOL_BELL,      // 4: Notifications
    LV_SYMBOL_LIST,      // 5: List
    LV_SYMBOL_DOWNLOAD,  // 6: Download
    LV_SYMBOL_POWER      // 7: Power
};

// State
static int selected_wedge = 0;
static avatar_state_t current_avatar_state = AVATAR_IDLE;
static bool ui_initialized = false;

// LVGL objects
static lv_obj_t* highlight_meter = NULL;
static lv_meter_scale_t* highlight_scale = NULL;
static lv_meter_indicator_t* highlight_arc = NULL;
static lv_obj_t* center_obj = NULL;
static lv_obj_t* avatar_img = NULL;
static lv_obj_t* wedge_labels_obj[8] = {0};

// Avatar image descriptors
static lv_img_dsc_t avatar_idle_dsc;
static lv_img_dsc_t avatar_listening_dsc;
static lv_img_dsc_t avatar_thinking_dsc;
static lv_img_dsc_t avatar_speaking_dsc;

// External LVGL mutex (from display.c)
extern SemaphoreHandle_t lvgl_mutex;

static void init_avatar_descriptors(void) {
    // Initialize image descriptors for LVGL
    avatar_idle_dsc.header.always_zero = 0;
    avatar_idle_dsc.header.w = AVATAR_SIZE;
    avatar_idle_dsc.header.h = AVATAR_SIZE;
    avatar_idle_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_idle_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_idle_dsc.data = (const uint8_t*)avatar_idle;
    
    avatar_listening_dsc.header.always_zero = 0;
    avatar_listening_dsc.header.w = AVATAR_SIZE;
    avatar_listening_dsc.header.h = AVATAR_SIZE;
    avatar_listening_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_listening_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_listening_dsc.data = (const uint8_t*)avatar_recording;  // Use recording avatar
    
    avatar_thinking_dsc.header.always_zero = 0;
    avatar_thinking_dsc.header.w = AVATAR_SIZE;
    avatar_thinking_dsc.header.h = AVATAR_SIZE;
    avatar_thinking_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_thinking_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_thinking_dsc.data = (const uint8_t*)avatar_thinking_1;  // Use thinking_1
    
    avatar_speaking_dsc.header.always_zero = 0;
    avatar_speaking_dsc.header.w = AVATAR_SIZE;
    avatar_speaking_dsc.header.h = AVATAR_SIZE;
    avatar_speaking_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_speaking_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_speaking_dsc.data = (const uint8_t*)avatar_speaking_1;  // Use speaking_1
}

static void update_highlight(void) {
    if (!highlight_meter || !highlight_arc) return;
    
    int start = selected_wedge * 45;
    int end = start + 43;
    lv_meter_set_indicator_start_value(highlight_meter, highlight_arc, start);
    lv_meter_set_indicator_end_value(highlight_meter, highlight_arc, end);
    
    // Update label colors
    for (int i = 0; i < 8; i++) {
        if (wedge_labels_obj[i]) {
            lv_color_t color = (i == selected_wedge) ? COLOR_TEXT : lv_color_white();
            lv_obj_set_style_text_color(wedge_labels_obj[i], color, 0);
        }
    }
}

bool wedge_ui_init(void) {
    ESP_LOGI(TAG, "Initializing wedge UI...");
    
    init_avatar_descriptors();
    
    lv_obj_t* screen = lv_scr_act();
    
    // Black background
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    
    // Create base meter for wedges
    lv_obj_t* meter = lv_meter_create(screen);
    lv_obj_set_size(meter, OUTER_RADIUS * 2, OUTER_RADIUS * 2);
    lv_obj_center(meter);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter, 0, 0);
    lv_obj_set_style_pad_all(meter, 0, 0);
    
    // Add scale
    lv_meter_scale_t* scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_range(meter, scale, 0, 360, 360, 270);
    lv_meter_set_scale_ticks(meter, scale, 0, 0, 0, lv_color_black());
    
    // Draw 8 arc segments
    for (int i = 0; i < 8; i++) {
        int start = i * 45;
        int end = start + 43;
        lv_color_t color = (i % 2 == 0) ? COLOR_WEDGE : COLOR_WEDGE_ALT;
        
        lv_meter_indicator_t* indic = lv_meter_add_arc(meter, scale, 
            OUTER_RADIUS - INNER_RADIUS, color, 0);
        lv_meter_set_indicator_start_value(meter, indic, start);
        lv_meter_set_indicator_end_value(meter, indic, end);
    }
    
    // Create highlight meter (overlays selected wedge)
    highlight_meter = lv_meter_create(screen);
    lv_obj_set_size(highlight_meter, OUTER_RADIUS * 2, OUTER_RADIUS * 2);
    lv_obj_center(highlight_meter);
    lv_obj_set_style_bg_opa(highlight_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(highlight_meter, 0, 0);
    lv_obj_set_style_pad_all(highlight_meter, 0, 0);
    
    highlight_scale = lv_meter_add_scale(highlight_meter);
    lv_meter_set_scale_range(highlight_meter, highlight_scale, 0, 360, 360, 270);
    lv_meter_set_scale_ticks(highlight_meter, highlight_scale, 0, 0, 0, lv_color_black());
    
    // Create highlight arc
    highlight_arc = lv_meter_add_arc(highlight_meter, highlight_scale, 
        OUTER_RADIUS - INNER_RADIUS, COLOR_SELECTED, 0);
    
    // Add labels for each wedge
    for (int i = 0; i < 8; i++) {
        float angle_deg = i * 45 + 22.5 - 90;
        float angle_rad = angle_deg * M_PI / 180.0;
        float label_radius = (OUTER_RADIUS + INNER_RADIUS) / 2;
        
        int label_x = CENTER_X + (int)(cos(angle_rad) * label_radius) - 10;
        int label_y = CENTER_Y + (int)(sin(angle_rad) * label_radius) - 10;
        
        lv_obj_t* label = lv_label_create(screen);
        lv_label_set_text(label, wedge_labels[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_pos(label, label_x, label_y);
        wedge_labels_obj[i] = label;
    }
    
    // Center circle
    center_obj = lv_obj_create(screen);
    lv_obj_set_size(center_obj, AVATAR_SIZE + 10, AVATAR_SIZE + 10);
    lv_obj_center(center_obj);
    lv_obj_set_style_radius(center_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_obj, COLOR_CENTER, 0);
    lv_obj_set_style_border_color(center_obj, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(center_obj, 3, 0);
    lv_obj_set_style_clip_corner(center_obj, true, 0);
    lv_obj_clear_flag(center_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Avatar image in center
    avatar_img = lv_img_create(center_obj);
    lv_img_set_src(avatar_img, &avatar_idle_dsc);
    lv_obj_center(avatar_img);
    
    ui_initialized = true;
    update_highlight();
    
    ESP_LOGI(TAG, "Wedge UI initialized");
    return true;
}

void wedge_ui_set_avatar_state(avatar_state_t state) {
    if (!ui_initialized || !avatar_img) return;
    if (state == current_avatar_state) return;
    
    current_avatar_state = state;
    
    const lv_img_dsc_t* img = &avatar_idle_dsc;
    switch (state) {
        case AVATAR_LISTENING:
            img = &avatar_listening_dsc;
            break;
        case AVATAR_THINKING:
            img = &avatar_thinking_dsc;
            break;
        case AVATAR_SPEAKING:
            img = &avatar_speaking_dsc;
            break;
        default:
            img = &avatar_idle_dsc;
            break;
    }
    
    lv_img_set_src(avatar_img, img);
}

void wedge_ui_set_selection(int wedge) {
    if (wedge < 0 || wedge > 7) return;
    if (wedge == selected_wedge) return;
    
    selected_wedge = wedge;
    if (ui_initialized) {
        update_highlight();
    }
}

int wedge_ui_get_selection(void) {
    return selected_wedge;
}

void wedge_ui_rotate(int delta) {
    int new_wedge = (selected_wedge + delta + 8) % 8;
    wedge_ui_set_selection(new_wedge);
}
