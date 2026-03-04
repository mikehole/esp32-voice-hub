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
#include "openai_client.h"
#include "conversation.h"
#include "status_ring.h"

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

// Status ring visualization (around avatar)
lv_obj_t* status_ring = NULL;
unsigned long recording_start_time = 0;
float status_animation_phase = 0;

// Colors for status ring
#define COLOR_RECORDING    lv_color_hex(0xE74C3C)  // Red for recording
#define COLOR_THINKING     lv_color_hex(0xF39C12)  // Orange for thinking
#define COLOR_SPEAKING     lv_color_hex(0x2ECC71)  // Green for speaking
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
void show_status_ring(ProcessingState state);
void hide_status_ring();
void update_status_ring();
void process_voice_command(const uint8_t* audio_data, size_t audio_size);

// Processing states
enum ProcessingState {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_THINKING,
    STATE_SPEAKING
};
static ProcessingState current_state = STATE_IDLE;

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
    
    // Inner pulsing ring (fixed size, opacity changes)
    ring_inner = lv_obj_create(recording_container);
    lv_obj_set_size(ring_inner, 35, 35);
    lv_obj_center(ring_inner);
    lv_obj_set_style_radius(ring_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring_inner, COLOR_REC_RING, 0);
    lv_obj_set_style_bg_opa(ring_inner, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ring_inner, 0, 0);
    lv_obj_clear_flag(ring_inner, LV_OBJ_FLAG_SCROLLABLE);
    
    // Middle pulsing ring (fixed size, border width changes)
    ring_middle = lv_obj_create(recording_container);
    lv_obj_set_size(ring_middle, 65, 65);
    lv_obj_center(ring_middle);
    lv_obj_set_style_radius(ring_middle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring_middle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring_middle, COLOR_REC_RING, 0);
    lv_obj_set_style_border_width(ring_middle, 2, 0);
    lv_obj_set_style_border_opa(ring_middle, LV_OPA_60, 0);
    lv_obj_clear_flag(ring_middle, LV_OBJ_FLAG_SCROLLABLE);
    
    // Outer pulsing ring (fixed size, border width changes)
    ring_outer = lv_obj_create(recording_container);
    lv_obj_set_size(ring_outer, 95, 95);
    lv_obj_center(ring_outer);
    lv_obj_set_style_radius(ring_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring_outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring_outer, COLOR_REC_RING, 0);
    lv_obj_set_style_border_width(ring_outer, 2, 0);
    lv_obj_set_style_border_opa(ring_outer, LV_OPA_40, 0);
    lv_obj_clear_flag(ring_outer, LV_OBJ_FLAG_SCROLLABLE);
    
    // "REC" label
    rec_label = lv_label_create(recording_container);
    lv_label_set_text(rec_label, "REC");
    lv_obj_set_style_text_color(rec_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rec_label, &lv_font_montserrat_14, 0);
    lv_obj_center(rec_label);
}

// Processing states
enum ProcessingState {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_THINKING,
    STATE_SPEAKING
};
static ProcessingState current_state = STATE_IDLE;
static lv_obj_t* state_label = NULL;

void show_recording_ui() {
    if (!recording_container) return;
    
    recording_start_time = millis();
    recording_ui_visible = true;
    current_state = STATE_RECORDING;
    
    // Hide normal center content
    lv_obj_add_flag(center_obj, LV_OBJ_FLAG_HIDDEN);
    
    // Show recording UI
    lv_obj_clear_flag(recording_container, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(duration_arc, 0);
    
    // Reset ring sizes
    lv_obj_set_size(ring_inner, 40, 40);
    lv_obj_set_size(ring_middle, 65, 65);
    lv_obj_set_size(ring_outer, 90, 90);
    
    // Update label
    if (rec_label) {
        lv_label_set_text(rec_label, LV_SYMBOL_AUDIO " REC");
        lv_obj_set_style_text_color(rec_label, lv_color_hex(0xFF5555), 0);
    }
}

void show_thinking_ui() {
    if (!recording_container) return;
    
    current_state = STATE_THINKING;
    recording_ui_visible = true;
    
    // Keep recording container visible but change appearance
    lv_obj_clear_flag(recording_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(center_obj, LV_OBJ_FLAG_HIDDEN);
    
    // Hide the pulsing rings
    if (ring_inner) lv_obj_add_flag(ring_inner, LV_OBJ_FLAG_HIDDEN);
    if (ring_middle) lv_obj_add_flag(ring_middle, LV_OBJ_FLAG_HIDDEN);
    if (ring_outer) lv_obj_add_flag(ring_outer, LV_OBJ_FLAG_HIDDEN);
    if (duration_arc) lv_obj_add_flag(duration_arc, LV_OBJ_FLAG_HIDDEN);
    
    // Show thinking indicator
    if (rec_label) {
        lv_label_set_text(rec_label, LV_SYMBOL_REFRESH " Thinking...");
        lv_obj_set_style_text_color(rec_label, lv_color_hex(0xFFAA00), 0);
    }
}

void show_speaking_ui() {
    if (!recording_container) return;
    
    current_state = STATE_SPEAKING;
    
    // Show speaking indicator
    if (rec_label) {
        lv_label_set_text(rec_label, LV_SYMBOL_VOLUME_MAX " Speaking");
        lv_obj_set_style_text_color(rec_label, lv_color_hex(0x55FF55), 0);
    }
}

void hide_recording_ui() {
    if (!recording_container) return;
    
    recording_ui_visible = false;
    current_state = STATE_IDLE;
    
    // Hide recording UI
    lv_obj_add_flag(recording_container, LV_OBJ_FLAG_HIDDEN);
    
    // Show rings again for next time
    if (ring_inner) lv_obj_clear_flag(ring_inner, LV_OBJ_FLAG_HIDDEN);
    if (ring_middle) lv_obj_clear_flag(ring_middle, LV_OBJ_FLAG_HIDDEN);
    if (ring_outer) lv_obj_clear_flag(ring_outer, LV_OBJ_FLAG_HIDDEN);
    if (duration_arc) lv_obj_clear_flag(duration_arc, LV_OBJ_FLAG_HIDDEN);
    
    // Show normal center content
    lv_obj_clear_flag(center_obj, LV_OBJ_FLAG_HIDDEN);
}

void update_recording_ui() {
    if (!recording_ui_visible) return;
    
    // Safety checks for all objects
    if (!recording_container || !duration_arc || !ring_inner || !ring_middle || !ring_outer) {
        return;
    }
    
    // Throttle updates to avoid overwhelming LVGL (every 50ms)
    static unsigned long last_update = 0;
    if (millis() - last_update < 50) return;
    last_update = millis();
    
    // Get audio level (0-100)
    uint8_t level = audio_get_level();
    
    // Debug: print level every 500ms
    static unsigned long last_debug = 0;
    if (millis() - last_debug > 500) {
        Serial.printf("Audio level: %d\n", level);
        last_debug = millis();
    }
    
    // Update duration arc (10 seconds = 100%)
    unsigned long elapsed = millis() - recording_start_time;
    int progress = (elapsed * 100) / 10000;  // 10 sec max
    if (progress > 100) progress = 100;
    lv_arc_set_value(duration_arc, progress);
    
    // Amplify level for more visible effect (levels are typically 8-20)
    // Map 0-25 input to 0-100 visual range
    int amplified = min(100, level * 4);
    
    // Pulse rings using opacity and border width (no resizing = no relayout)
    // Inner ring: opacity pulses with level (20% to 100%)
    lv_opa_t inner_opa = LV_OPA_20 + (amplified * (LV_OPA_100 - LV_OPA_20) / 100);
    lv_obj_set_style_bg_opa(ring_inner, inner_opa, 0);
    
    // Middle ring: border width 2-12 based on level, high opacity range
    int middle_border = 2 + (amplified * 10 / 100);
    lv_obj_set_style_border_width(ring_middle, middle_border, 0);
    lv_opa_t middle_opa = LV_OPA_20 + (amplified * (LV_OPA_100 - LV_OPA_20) / 100);
    lv_obj_set_style_border_opa(ring_middle, middle_opa, 0);
    
    // Outer ring: border width 1-10 based on level
    int outer_border = 1 + (amplified * 9 / 100);
    lv_obj_set_style_border_width(ring_outer, outer_border, 0);
    lv_opa_t outer_opa = LV_OPA_10 + (amplified * (LV_OPA_90 - LV_OPA_10) / 100);
    lv_obj_set_style_border_opa(ring_outer, outer_opa, 0);
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

// Process voice command after recording stops
void process_voice_command(const uint8_t* audio_data, size_t audio_size) {
    if (audio_size < 1000) {
        Serial.println("Recording too short, ignoring");
        status_ring_hide();
        return;
    }
    
    // Show thinking state (orange pulsing ring)
    status_ring_show(STATE_THINKING);
    lv_task_handler();
    Serial.println("Processing voice command...");
    
    // Step 1: Transcribe with Whisper
    char* transcript = openai_transcribe(audio_data, audio_size);
    if (!transcript || strlen(transcript) == 0) {
        Serial.printf("Transcription failed: %s\n", openai_get_last_error());
        if (transcript) free(transcript);
        status_ring_hide();
        return;
    }
    
    Serial.printf("Transcript: '%s'\n", transcript);
    
    // Step 2: Send to OpenClaw with conversation history
    char* response = openclaw_send_with_history(transcript);
    free(transcript);
    
    if (!response || strlen(response) == 0) {
        Serial.printf("OpenClaw failed: %s\n", openai_get_last_error());
        status_ring_hide();
        return;
    }
    
    Serial.printf("Response: '%s'\n", response);
    
    // Step 3: Convert response to speech
    size_t tts_size = 0;
    uint8_t* tts_audio = openai_tts(response, &tts_size);
    free(response);
    
    if (!tts_audio) {
        Serial.printf("TTS failed: %s\n", openai_get_last_error());
        status_ring_hide();
        return;
    }
    
    // Step 4: Show speaking state (green wiggling ring) and play
    status_ring_show(STATE_SPEAKING);
    lv_task_handler();
    Serial.printf("Playing TTS: %u bytes\n", tts_size);
    audio_play(tts_audio, tts_size, 24000);
    heap_caps_free(tts_audio);
    
    // Return to idle
    status_ring_hide();
}

void check_touch() {
    uint16_t x, y;
    uint8_t touched = getTouch(&x, &y);
    
    // Press-and-hold for recording when Minerva selected
    ProcessingState ring_state = status_ring_get_state();
    if (selected_wedge == 0 && ring_state != STATE_THINKING && ring_state != STATE_SPEAKING) {
        // Touch DOWN on center = start recording
        if (touched && !was_touched && is_center_touch(x, y)) {
            Serial.println("Center touch DOWN - start recording");
            if (audio_start_recording()) {
                status_ring_show(STATE_RECORDING);
                recording_start_time = millis();
            }
            was_touched = touched;
            return;
        }
        
        // Touch UP while recording = stop and process
        if (!touched && was_touched && audio_is_recording()) {
            Serial.println("Touch UP - stop recording");
            size_t audio_size = 0;
            const uint8_t* audio_data = audio_stop_recording(&audio_size);
            Serial.printf("Audio captured: %u bytes\n", audio_size);
            
            // Process voice command
            process_voice_command(audio_data, audio_size);
            
            was_touched = touched;
            return;
        }
    }
    
    if (touched && !was_touched) {
        // New touch detected (for wedge selection)
        Serial.printf("Touch at: %d, %d\n", x, y);
        
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
    delay(2000);  // Wait for serial monitor to connect
    Serial.println("\n\n========================================");
    Serial.println("ESP32 Voice Hub - Starting...");
    Serial.println("========================================\n");
    
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
    
    // Initialize OpenAI client
    openai_init();
    
    // Initialize conversation history (requires SD card)
    if (conversation_init()) {
        Serial.printf("Conversation: %d messages loaded\n", conversation_get_count());
    }
    
    create_radial_ui();
    
    // Initialize status ring (after UI is created)
    status_ring_init(lv_scr_act());
    
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
    status_ring_update();  // Update animated status ring
    delay(10);
}
