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
#include "avatar.h"
#include "bidi_switch_knob.h"
#include "wifi_manager.h"
#include "web_admin.h"
#include "audio_capture.h"
#include "openai_client.h"
#include "conversation.h"
#include "status_ring.h"
#include "notification.h"
#include "lvgl_port.h"

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
    "News", "Timer", "Zoom", "Settings"
};

const char* wedge_icons[] = {
    NULL,                // Minerva - uses avatar instead
    LV_SYMBOL_AUDIO,     // Music
    LV_SYMBOL_HOME,      // Home  
    LV_SYMBOL_EYE_OPEN,  // Weather (eye = looking outside)
    LV_SYMBOL_LIST,      // News
    LV_SYMBOL_BELL,      // Timer
    LV_SYMBOL_VIDEO,     // Zoom (video call)
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
void process_voice_command(const uint8_t* audio_data, size_t audio_size);

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
    
    // Initialize avatar system (for Minerva state images)
    avatar_init(center_obj);
    
    ui_initialized = true;
    
    // Apply initial selection
    update_selection();
}

// Update just the selection highlight - no object destruction!
void update_selection() {
    if (!ui_initialized) return;
    if (!lvgl_port_lock(50)) return;  // Skip if can't get mutex
    
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
    
    // Always show avatar, hide icon text
    lv_label_set_text(center_icon, "");
    lv_obj_t* avatar_obj = avatar_get_obj();
    if (avatar_obj) {
        lv_obj_clear_flag(avatar_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(avatar_obj);
    }
    
    // Invalidate highlight meter for clean redraw
    if (highlight_meter) {
        lv_obj_invalidate(highlight_meter);
    }
    
    lvgl_port_unlock();
    
    // Update center avatar based on selected wedge (has its own mutex)
    avatar_set_wedge(selected_wedge);
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

// Speak notification text via TTS
void speak_notification(const char* text) {
    if (!text || strlen(text) == 0) return;
    
    Serial.printf("Speaking notification: '%s'\n", text);
    
    // Stop any playing audio (attention chime) and wait for it to fully stop
    audio_stop_playback();
    
    // Wait longer for audio hardware to fully settle
    // The I2S DMA buffers need time to drain
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Show speaking state
    avatar_set_state(STATE_SPEAKING);
    status_ring_show(STATE_SPEAKING);
    
    // Get TTS audio
    size_t audio_size = 0;
    uint8_t* audio_data = openai_tts(text, &audio_size);
    
    if (audio_data) {
        // Play audio (24kHz PCM from OpenAI)
        audio_play(audio_data, audio_size, 24000);
        heap_caps_free(audio_data);
        
        // Wait for playback to finish
        while (audio_is_playing()) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    } else {
        Serial.printf("TTS failed: %s\n", openai_get_last_error());
    }
    
    // Return to idle
    status_ring_hide();
    avatar_set_state(STATE_IDLE);
}

// Process voice command after recording stops
// Background task for voice processing
static TaskHandle_t voice_task_handle = NULL;
static const uint8_t* pending_audio_data = NULL;
static size_t pending_audio_size = 0;
static volatile bool voice_processing = false;
static uint8_t* tts_result = NULL;
static size_t tts_result_size = 0;
static volatile int voice_stage = 0;  // 0=idle, 1=transcribing, 2=thinking, 3=tts, 4=done, -1=error

// Background task that runs API calls
void voice_task(void* param) {
    while (true) {
        // Wait for work
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (!pending_audio_data || pending_audio_size < 1000) {
            voice_stage = -1;
            voice_processing = false;
            continue;
        }
        
        voice_stage = 1;  // Transcribing
        Serial.println("Background: Transcribing...");
        
        // Step 1: Transcribe with Whisper
        char* transcript = openai_transcribe(pending_audio_data, pending_audio_size);
        if (!transcript || strlen(transcript) == 0) {
            Serial.printf("Background: Transcription failed: %s\n", openai_get_last_error());
            if (transcript) free(transcript);
            voice_stage = -1;
            voice_processing = false;
            continue;
        }
        
        Serial.printf("Background: Transcript: '%s'\n", transcript);
        voice_stage = 2;  // Thinking (OpenClaw)
        
        // Step 2: Send to OpenClaw
        char* response = openclaw_send_with_history(transcript);
        free(transcript);
        
        if (!response || strlen(response) == 0) {
            Serial.printf("Background: OpenClaw failed: %s\n", openai_get_last_error());
            voice_stage = -1;
            voice_processing = false;
            continue;
        }
        
        Serial.printf("Background: Response: '%s'\n", response);
        voice_stage = 3;  // TTS
        
        // Step 3: Convert to speech
        tts_result = openai_tts(response, &tts_result_size);
        free(response);
        
        if (!tts_result) {
            Serial.printf("Background: TTS failed: %s\n", openai_get_last_error());
            voice_stage = -1;
            voice_processing = false;
            continue;
        }
        
        Serial.printf("Background: TTS ready: %u bytes\n", tts_result_size);
        voice_stage = 4;  // Done - ready to play
    }
}

void process_voice_command(const uint8_t* audio_data, size_t audio_size) {
    if (audio_size < 1000) {
        Serial.println("Recording too short, ignoring");
        status_ring_hide();
        return;
    }
    
    // Create background task if needed
    if (!voice_task_handle) {
        xTaskCreatePinnedToCore(voice_task, "voice", 16384, NULL, 1, &voice_task_handle, 0);
    }
    
    // Start background processing
    pending_audio_data = audio_data;
    pending_audio_size = audio_size;
    voice_processing = true;
    voice_stage = 1;
    tts_result = NULL;
    tts_result_size = 0;
    
    // Hide recording ring, show thinking avatar
    status_ring_hide();
    avatar_set_state(STATE_THINKING);
    Serial.println("Processing voice command in background...");
    
    // Notify background task to start
    xTaskNotifyGive(voice_task_handle);
}

// Called from loop() to check voice processing status
void check_voice_processing() {
    if (!voice_processing) return;
    
    if (voice_stage == 4) {
        // TTS ready - play it with speaking animation!
        avatar_set_state(STATE_SPEAKING);
        status_ring_show(STATE_SPEAKING);  // Cyan pulsing ring
        lvgl_port_task_handler();  // Process pending UI updates
        Serial.printf("Playing TTS: %u bytes\n", tts_result_size);
        audio_play(tts_result, tts_result_size, 24000);
        heap_caps_free(tts_result);
        tts_result = NULL;
        tts_result_size = 0;
        voice_stage = 0;
        voice_processing = false;
        status_ring_hide();
        avatar_set_state(STATE_IDLE);  // Return to idle
    } else if (voice_stage == -1) {
        // Error occurred
        Serial.println("Voice processing failed");
        voice_stage = 0;
        voice_processing = false;
        status_ring_hide();
        avatar_set_state(STATE_IDLE);  // Return to idle
    }
    // Stages 1-3: still processing, avatar shows thinking
}

// Forward declaration for notification TTS
void speak_notification(const char* text);

void check_touch() {
    uint16_t x, y;
    uint8_t touched = getTouch(&x, &y);
    
    // Check for notification acknowledgment first (highest priority)
    ProcessingState ring_state = status_ring_get_state();
    if (notification_pending() && ring_state == STATE_NOTIFICATION) {
        if (touched && !was_touched && is_center_touch(x, y)) {
            Serial.println("Tap - acknowledge notification");
            
            NotifyType type = notification_get_type();
            
            if (type == NOTIFY_AUDIO) {
                // Pre-loaded audio - play directly
                size_t audio_size = 0;
                uint32_t sample_rate = 0;
                const uint8_t* audio = notification_get_audio(&audio_size, &sample_rate);
                
                notification_acknowledge();  // Clear the notification
                
                if (audio && audio_size > 0) {
                    // Stop attention sound and wait
                    audio_stop_playback();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    // Show speaking state
                    avatar_set_state(STATE_SPEAKING);
                    status_ring_show(STATE_SPEAKING);
                    
                    Serial.printf("Playing notification audio: %u bytes @ %u Hz\n", audio_size, sample_rate);
                    audio_play(audio, audio_size, sample_rate);
                    
                    // Wait for playback, updating UI while we wait
                    while (audio_is_playing()) {
                        lvgl_port_task_handler();
                        status_ring_update();
                        vTaskDelay(pdMS_TO_TICKS(30));
                    }
                    
                    // Free the audio buffer now that playback is done
                    notification_free_audio();
                    
                    // Return to idle
                    status_ring_hide();
                    avatar_set_state(STATE_IDLE);
                }
            } else {
                // Text notification - do TTS
                const char* text = notification_acknowledge();
                if (text) {
                    speak_notification(text);
                }
            }
            was_touched = touched;
            return;
        }
    }
    
    // Tap-to-toggle recording when Minerva selected
    if (selected_wedge == 0 && ring_state != STATE_THINKING && ring_state != STATE_SPEAKING && ring_state != STATE_NOTIFICATION) {
        // Tap center to start/stop recording
        if (touched && !was_touched && is_center_touch(x, y)) {
            if (audio_is_recording()) {
                // Currently recording - stop and process
                Serial.println("Tap - stop recording");
                size_t audio_size = 0;
                const uint8_t* audio_data = audio_stop_recording(&audio_size);
                Serial.printf("Audio captured: %u bytes\n", audio_size);
                
                // Process voice command
                process_voice_command(audio_data, audio_size);
            } else {
                // Not recording - start
                Serial.println("Tap - start recording");
                if (audio_start_recording()) {
                    avatar_set_state(STATE_RECORDING);  // Listening pose
                    status_ring_show(STATE_RECORDING);  // Red pulsing ring
                }
            }
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
    
    // Initialize LVGL mutex BEFORE any LVGL operations
    lvgl_port_init();
    
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
    
    // Initialize notification system
    notification_init();
    
    // Create UI (lock mutex for safety during init)
    if (lvgl_port_lock(1000)) {
        create_radial_ui();
        
        // Initialize status ring (after UI is created) - still used for recording
        status_ring_init(lv_scr_act());
        lvgl_port_unlock();
    } else {
        Serial.println("ERROR: Could not get LVGL mutex for UI init!");
    }
    
    // Show connecting avatar + ring if not connected yet
    WiFiState wifi_state = wifi_manager_get_state();
    if (wifi_state != WIFI_STATE_CONNECTED) {
        avatar_set_state(STATE_CONNECTING);
        status_ring_show(STATE_CONNECTING);
        Serial.println("Waiting for WiFi connection...");
    }
    
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

// Forward declaration
void check_voice_processing();

void loop() {
    // Use mutex-protected LVGL handler
    lvgl_port_task_handler();
    
    // Check voice processing status (background task)
    check_voice_processing();
    
    // Periodic heap check (every 10 seconds)
    static unsigned long last_heap_check = 0;
    if (millis() - last_heap_check > 10000) {
        Serial.printf("Heap: %u free, %u largest block, PSRAM: %u free\n",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
        last_heap_check = millis();
    }
    
    // Check WiFi state and update avatar + ring
    static WiFiState last_wifi_state = WIFI_STATE_IDLE;
    WiFiState wifi_state = wifi_manager_get_state();
    
    if (wifi_state != last_wifi_state) {
        if (wifi_state == WIFI_STATE_CONNECTED) {
            // Just connected - show idle avatar, hide ring
            avatar_set_state(STATE_IDLE);
            status_ring_hide();
            Serial.println("WiFi connected - ready!");
        } else if (wifi_state == WIFI_STATE_CONNECTING) {
            // Connecting - show zapped avatar WITH spinning ring!
            avatar_set_state(STATE_CONNECTING);
            status_ring_show(STATE_CONNECTING);
            Serial.println("WiFi connecting...");
        }
        last_wifi_state = wifi_state;
    }
    
    // Only allow touch/encoder when connected
    if (wifi_state == WIFI_STATE_CONNECTED) {
        check_touch();
        check_encoder();
        
        // Update notification system (plays attention sound periodically)
        notification_update();
    }
    
    wifi_manager_loop();
    
    // Update status ring
    status_ring_update();
    
    delay(10);
}
