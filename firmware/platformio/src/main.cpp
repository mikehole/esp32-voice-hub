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

// Recording visualization
lv_obj_t* recording_container = NULL;
lv_obj_t* ring_inner = NULL;
lv_obj_t* ring_middle = NULL;
lv_obj_t* ring_outer = NULL;
lv_obj_t* duration_arc = NULL;
lv_obj_t* rec_label = NULL;
unsigned long recording_start_time = 0;
bool recording_ui_visible = false;

// Colors for recording UI
#define COLOR_REC_RING     lv_color_hex(0xE74C3C)  // Red for recording
#define COLOR_REC_DIM      lv_color_hex(0x5C1F1A)  // Dim red
#define COLOR_REC_ARC      lv_color_hex(0x3498DB)  // Blue duration arc

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
void show_recording_ui();
void hide_recording_ui();
void update_recording_ui();

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

// Create recording UI (hidden by default)
void create_recording_ui() {
    lv_obj_t* screen = lv_scr_act();
    
    // Container for all recording elements
    recording_container = lv_obj_create(screen);
    lv_obj_set_size(recording_container, 130, 130);
    lv_obj_center(recording_container);
    lv_obj_set_style_radius(recording_container, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(recording_container, COLOR_CENTER, 0);
    lv_obj_set_style_border_width(recording_container, 0, 0);
    lv_obj_set_style_pad_all(recording_container, 0, 0);
    lv_obj_clear_flag(recording_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(recording_container, LV_OBJ_FLAG_HIDDEN);
    
    // Duration arc (outer ring showing time progress)
    duration_arc = lv_arc_create(recording_container);
    lv_obj_set_size(duration_arc, 120, 120);
    lv_obj_center(duration_arc);
    lv_arc_set_rotation(duration_arc, 270);
    lv_arc_set_bg_angles(duration_arc, 0, 360);
    lv_arc_set_range(duration_arc, 0, 100);
    lv_arc_set_value(duration_arc, 0);
    lv_obj_set_style_arc_color(duration_arc, COLOR_REC_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_color(duration_arc, COLOR_REC_ARC, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(duration_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(duration_arc, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(duration_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(duration_arc, LV_OBJ_FLAG_CLICKABLE);
    
    // Inner pulsing ring
    ring_inner = lv_obj_create(recording_container);
    lv_obj_set_size(ring_inner, 40, 40);
    lv_obj_center(ring_inner);
    lv_obj_set_style_radius(ring_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring_inner, COLOR_REC_RING, 0);
    lv_obj_set_style_bg_opa(ring_inner, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ring_inner, 0, 0);
    
    // Middle pulsing ring
    ring_middle = lv_obj_create(recording_container);
    lv_obj_set_size(ring_middle, 65, 65);
    lv_obj_center(ring_middle);
    lv_obj_set_style_radius(ring_middle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring_middle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring_middle, COLOR_REC_RING, 0);
    lv_obj_set_style_border_width(ring_middle, 3, 0);
    lv_obj_set_style_border_opa(ring_middle, LV_OPA_60, 0);
    
    // Outer pulsing ring  
    ring_outer = lv_obj_create(recording_container);
    lv_obj_set_size(ring_outer, 90, 90);
    lv_obj_center(ring_outer);
    lv_obj_set_style_radius(ring_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring_outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring_outer, COLOR_REC_RING, 0);
    lv_obj_set_style_border_width(ring_outer, 2, 0);
    lv_obj_set_style_border_opa(ring_outer, LV_OPA_40, 0);
    
    // "REC" label
    rec_label = lv_label_create(recording_container);
    lv_label_set_text(rec_label, "REC");
    lv_obj_set_style_text_color(rec_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rec_label, &lv_font_montserrat_14, 0);
    lv_obj_center(rec_label);
}

void show_recording_ui() {
    if (!recording_container) return;
    
    recording_start_time = millis();
    recording_ui_visible = true;
    
    // Hide normal center content
    lv_obj_add_flag(center_obj, LV_OBJ_FLAG_HIDDEN);
    
    // Show recording UI
    lv_obj_clear_flag(recording_container, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(duration_arc, 0);
    
    // Reset ring sizes
    lv_obj_set_size(ring_inner, 40, 40);
    lv_obj_set_size(ring_middle, 65, 65);
    lv_obj_set_size(ring_outer, 90, 90);
}

void hide_recording_ui() {
    if (!recording_container) return;
    
    recording_ui_visible = false;
    
    // Hide recording UI
    lv_obj_add_flag(recording_container, LV_OBJ_FLAG_HIDDEN);
    
    // Show normal center content
    lv_obj_clear_flag(center_obj, LV_OBJ_FLAG_HIDDEN);
}

void update_recording_ui() {
    if (!recording_ui_visible || !recording_container) return;
    
    // Get audio level (0-100)
    uint8_t level = audio_get_level();
    
    // Update duration arc (10 seconds = 100%)
    unsigned long elapsed = millis() - recording_start_time;
    int progress = (elapsed * 100) / 10000;  // 10 sec max
    if (progress > 100) progress = 100;
    lv_arc_set_value(duration_arc, progress);
    
    // Pulse rings based on audio level
    // Inner ring: scales 30-50 based on level
    int inner_size = 30 + (level * 20 / 100);
    lv_obj_set_size(ring_inner, inner_size, inner_size);
    lv_obj_center(ring_inner);
    
    // Middle ring: scales 50-75 based on level
    int middle_size = 50 + (level * 25 / 100);
    lv_obj_set_size(ring_middle, middle_size, middle_size);
    lv_obj_center(ring_middle);
    
    // Outer ring: scales 70-100 based on level
    int outer_size = 70 + (level * 30 / 100);
    lv_obj_set_size(ring_outer, outer_size, outer_size);
    lv_obj_center(ring_outer);
    
    // Adjust opacity based on level for more visual feedback
    lv_opa_t inner_opa = LV_OPA_50 + (level * (LV_OPA_100 - LV_OPA_50) / 100);
    lv_obj_set_style_bg_opa(ring_inner, inner_opa, 0);
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
    
    // Create recording visualization (hidden initially)
    create_recording_ui();
    
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
                    hide_recording_ui();
                    // TODO: Send to OpenClaw for transcription
                } else {
                    // Start recording
                    if (audio_start_recording()) {
                        Serial.println("Recording started - touch center again to stop");
                        show_recording_ui();
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
    update_recording_ui();  // Update pulsing rings if recording
    delay(10);
}
