/**
 * ESP32 Voice Hub - Main Application
 * Radial Wedge UI with 8 segments (Trivial Pursuit style)
 * Touch-enabled wedge selection
 */

#include <Arduino.h>
#include <math.h>
#include "lcd_bsp.h"
#include "cst816.h"
#include "lcd_bl_pwm_bsp.h"
#include "lcd_config.h"
#include "images/minerva_img.h"
#include "bidi_switch_knob.h"
#include "wifi_manager.h"
#include "web_admin.h"
#include "audio_capture.h"

// Encoder pins
#define ENCODER_PIN_A    8
#define ENCODER_PIN_B    7

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
#define OUTER_RADIUS    165
#define INNER_RADIUS    85
#define CENTER_RADIUS   78

// Wedge labels and icons
const char* wedge_labels[] = {
    "Minerva", "Music", "Home", "Weather",
    "News", "Timer", "Lights", "Settings"
};

const char* wedge_icons[] = {
    NULL,                // Minerva - uses avatar instead
    LV_SYMBOL_AUDIO,     // Music
    LV_SYMBOL_HOME,      // Home  
    LV_SYMBOL_EYE_OPEN,  // Weather (eye = looking outside)
    LV_SYMBOL_LIST,      // News
    LV_SYMBOL_BELL,      // Timer
    LV_SYMBOL_CHARGE,    // Lights (power/energy)
    LV_SYMBOL_SETTINGS   // Settings
};

// Global state
int selected_wedge = 0;
lv_obj_t* wedge_labels_obj[8];
lv_obj_t* center_icon = NULL;
lv_obj_t* center_obj = NULL;
lv_obj_t* highlight_meter = NULL;  // Separate meter for highlight arc
lv_meter_indicator_t* highlight_arc = NULL;
lv_meter_scale_t* highlight_scale = NULL;
uint16_t last_touch_x = 0;
uint16_t last_touch_y = 0;
bool was_touched = false;
bool ui_initialized = false;

// Encoder state
static knob_handle_t knob_handle = NULL;
volatile bool knob_left_flag = false;
volatile bool knob_right_flag = false;

// Brightness state
static int current_brightness = 100;

// Brightness callbacks for web admin
int get_brightness() {
    return current_brightness;
}

void set_brightness_value(int value) {
    current_brightness = value;
    // Map 0-100 to 0-255 for PWM
    uint16_t duty = (value * 255) / 100;
    setUpdutySubdivide(duty);
    Serial.printf("Brightness set to %d%% (duty: %d)\n", value, duty);
}

// Forward declarations
void update_selection();

// Encoder callbacks
static void knob_left_cb(void *arg, void *data) {
    knob_left_flag = true;
}

static void knob_right_cb(void *arg, void *data) {
    knob_right_flag = true;
}

// Calculate which wedge was touched based on x,y coordinates
int get_touched_wedge(int x, int y) {
    // Invert touch coordinates to match display orientation
    x = SCREEN_SIZE - x;
    y = SCREEN_SIZE - y;
    
    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;
    float distance = sqrt(dx * dx + dy * dy);
    
    // Check if touch is in the wedge ring (not center, not outside)
    if (distance < INNER_RADIUS || distance > OUTER_RADIUS) {
        return -1;  // Not in wedge area
    }
    
    // Calculate angle (0 = right, going counter-clockwise)
    float angle = atan2(dy, dx) * 180.0 / M_PI;
    
    // Convert to our coordinate system (0 = top, clockwise)
    angle = angle + 90;  // Shift so 0 is at top
    if (angle < 0) angle += 360;
    
    // Calculate wedge index (each wedge is 45 degrees)
    int wedge = (int)(angle / 45.0) % 8;
    
    return wedge;
}

void create_radial_ui() {
    lv_obj_t* screen = lv_scr_act();
    
    // Black background
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    
    // Create base meter for the pie chart (static, never changes)
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
    
    // Draw 8 arc segments - all in base colors (no selection highlight here)
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
    
    // Create the highlight arc
    highlight_arc = lv_meter_add_arc(highlight_meter, highlight_scale, 
        OUTER_RADIUS - INNER_RADIUS, COLOR_SELECTED, 0);
    
    // Add labels for each wedge
    for (int i = 0; i < 8; i++) {
        float angle_deg = i * 45 + 22.5 - 90;
        float angle_rad = angle_deg * M_PI / 180.0;
        float label_radius = (OUTER_RADIUS + INNER_RADIUS) / 2;
        
        int label_x = CENTER_X + (int)(cos(angle_rad) * label_radius) - 22;
        int label_y = CENTER_Y + (int)(sin(angle_rad) * label_radius) - 8;
        
        lv_obj_t* label = lv_label_create(screen);
        lv_label_set_text(label, wedge_labels[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
        lv_obj_set_pos(label, label_x, label_y);
        wedge_labels_obj[i] = label;
    }
    
    // Center circle (on top) - larger to fit avatar
    center_obj = lv_obj_create(screen);
    lv_obj_set_size(center_obj, 130, 130);
    lv_obj_center(center_obj);
    lv_obj_set_style_radius(center_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_obj, COLOR_CENTER, 0);
    lv_obj_set_style_border_color(center_obj, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(center_obj, 3, 0);
    lv_obj_set_style_clip_corner(center_obj, true, 0);
    lv_obj_clear_flag(center_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Center icon (created once, updated on selection change)
    center_icon = lv_label_create(center_obj);
    lv_obj_set_style_text_font(center_icon, &lv_font_montserrat_32, 0);
    lv_obj_center(center_icon);
    
    ui_initialized = true;
    
    // Apply initial selection
    update_selection();
}

// Update just the selection highlight - no object destruction!
void update_selection() {
    if (!ui_initialized) return;
    
    // Move highlight arc to selected wedge
    int start = selected_wedge * 45;
    int end = start + 43;
    lv_meter_set_indicator_start_value(highlight_meter, highlight_arc, start);
    lv_meter_set_indicator_end_value(highlight_meter, highlight_arc, end);
    
    // Update label colors
    for (int i = 0; i < 8; i++) {
        if (wedge_labels_obj[i]) {
            if (i == selected_wedge) {
                lv_obj_set_style_text_color(wedge_labels_obj[i], COLOR_CENTER, 0);
            } else {
                lv_obj_set_style_text_color(wedge_labels_obj[i], COLOR_TEXT, 0);
            }
        }
    }
    
    // Update center content
    if (selected_wedge == 0) {
        // Minerva - show avatar
        lv_label_set_text(center_icon, "");  // Hide icon
        // Check if avatar already exists
        lv_obj_t* existing_avatar = lv_obj_get_child(center_obj, 0);
        if (existing_avatar && existing_avatar != center_icon) {
            // Avatar exists, ensure it's visible
            lv_obj_clear_flag(existing_avatar, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Create avatar
            lv_obj_t* avatar = lv_img_create(center_obj);
            lv_img_set_src(avatar, &minerva_avatar);
            lv_obj_center(avatar);
            lv_obj_move_background(avatar);  // Put behind icon
        }
        lv_obj_set_style_text_color(center_icon, COLOR_SELECTED, 0);
    } else {
        // Other - show icon, hide avatar if exists
        lv_obj_t* child = lv_obj_get_child(center_obj, 0);
        while (child) {
            if (child != center_icon) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
            child = lv_obj_get_child(center_obj, lv_obj_get_index(child) + 1);
        }
        lv_label_set_text(center_icon, wedge_icons[selected_wedge]);
        lv_obj_set_style_text_color(center_icon, COLOR_SELECTED, 0);
        lv_obj_center(center_icon);
    }
}

void rebuild_ui() {
    // No more destruction - just update the selection highlight
    update_selection();
}

// Check if touch is in center circle
bool is_center_touch(uint16_t x, uint16_t y) {
    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;
    int dist_sq = dx * dx + dy * dy;
    return dist_sq < (65 * 65);  // ~65px radius for center circle
}

void check_touch() {
    uint16_t x, y;
    uint8_t touched = getTouch(&x, &y);
    
    if (touched && !was_touched) {
        // New touch detected
        Serial.printf("Touch at: %d, %d\n", x, y);
        
        // Check if center touched (for Minerva recording)
        if (is_center_touch(x, y)) {
            Serial.println("Center touched!");
            
            if (selected_wedge == 0) {  // Minerva selected
                if (audio_is_recording()) {
                    // Stop recording
                    size_t audio_size = 0;
                    const uint8_t* audio_data = audio_stop_recording(&audio_size);
                    Serial.printf("Audio captured: %u bytes\n", audio_size);
                    // TODO: Send to OpenClaw for transcription
                } else {
                    // Start recording
                    if (audio_start_recording()) {
                        Serial.println("Recording started - touch center again to stop");
                    }
                }
            }
            was_touched = touched;
            return;
        }
        
        int wedge = get_touched_wedge(x, y);
        Serial.printf("Wedge: %d\n", wedge);
        
        if (wedge >= 0 && wedge != selected_wedge) {
            selected_wedge = wedge;
            Serial.printf("Selected: %s\n", wedge_labels[selected_wedge]);
            update_selection();
        }
    }
    
    was_touched = touched;
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Voice Hub - Starting...");
    
    Touch_Init();
    Serial.println("Touch initialized");
    
    lcd_lvgl_Init();
    Serial.println("LCD initialized");
    
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_200);
    Serial.println("Backlight initialized");
    
    // Initialize rotary encoder
    knob_config_t knob_cfg = {
        .gpio_encoder_a = ENCODER_PIN_A,
        .gpio_encoder_b = ENCODER_PIN_B,
    };
    knob_handle = iot_knob_create(&knob_cfg);
    if (knob_handle != NULL) {
        iot_knob_register_cb(knob_handle, KNOB_LEFT, knob_left_cb, NULL);
        iot_knob_register_cb(knob_handle, KNOB_RIGHT, knob_right_cb, NULL);
        Serial.println("Encoder initialized");
    } else {
        Serial.println("Encoder init failed!");
    }
    
    // Initialize WiFi manager
    wifi_manager_init();
    Serial.printf("WiFi: %s\n", wifi_manager_get_status());
    
    // Set up brightness callbacks for web admin
    web_admin_set_brightness_callbacks(get_brightness, set_brightness_value);
    
    // Initialize audio
    if (audio_init()) {
        Serial.println("Audio: Ready");
    } else {
        Serial.println("Audio: Init failed!");
    }
    
    create_radial_ui();
    
    Serial.println("Setup complete! Touch or rotate to select.");
}

void check_encoder() {
    static unsigned long last_encoder_time = 0;
    const unsigned long DEBOUNCE_MS = 30;  // Fast response now that we don't rebuild
    
    if (millis() - last_encoder_time < DEBOUNCE_MS) {
        knob_left_flag = false;
        knob_right_flag = false;
        return;
    }
    
    bool left = knob_left_flag;
    bool right = knob_right_flag;
    knob_left_flag = false;
    knob_right_flag = false;
    
    if (left && !right) {
        selected_wedge = (selected_wedge + 7) % 8;
        Serial.printf("Encoder LEFT - Selected: %s\n", wedge_labels[selected_wedge]);
        update_selection();
        last_encoder_time = millis();
    } else if (right && !left) {
        selected_wedge = (selected_wedge + 1) % 8;
        Serial.printf("Encoder RIGHT - Selected: %s\n", wedge_labels[selected_wedge]);
        update_selection();
        last_encoder_time = millis();
    }
}

void loop() {
    lv_timer_handler();
    check_touch();
    check_encoder();
    wifi_manager_loop();
    delay(10);
}
