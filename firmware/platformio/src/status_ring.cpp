/**
 * Status Ring - Animated concentric rings around avatar for visual feedback
 */

#include "status_ring.h"
#include "audio_capture.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Mutex to protect LVGL access (LVGL is not thread-safe)
static SemaphoreHandle_t lvgl_mutex = NULL;

// Colors
#define COLOR_CONNECTING   lv_color_hex(0x3498DB)  // Blue for connecting
#define COLOR_RECORDING    lv_color_hex(0xE74C3C)  // Red
#define COLOR_THINKING     lv_color_hex(0xF39C12)  // Orange  
#define COLOR_SPEAKING     lv_color_hex(0x00A5C8)  // Muted cyan (matches circuit traces)
#define COLOR_RING_BG      lv_color_hex(0x1A1A1A)  // Dark background

// Ring objects - multiple concentric rings
static lv_obj_t* rings[STATUS_RING_COUNT] = {NULL, NULL, NULL};
static ProcessingState current_state = STATE_IDLE;
static float animation_phase = 0;
static unsigned long start_time = 0;

// Ring sizes (inner to outer) - tighter spacing, no gaps
static const int ring_sizes[STATUS_RING_COUNT] = {140, 146, 152};
static const int ring_widths[STATUS_RING_COUNT] = {4, 4, 4};

void status_ring_init(lv_obj_t* parent) {
    if (!parent) {
        Serial.println("Status ring: ERROR - null parent!");
        return;
    }
    
    // Create concentric arcs (outer to inner so inner draws on top)
    for (int i = STATUS_RING_COUNT - 1; i >= 0; i--) {
        rings[i] = lv_arc_create(parent);
        if (!rings[i]) {
            Serial.printf("Status ring: ERROR - failed to create ring %d!\n", i);
            return;
        }
        
        lv_obj_set_size(rings[i], ring_sizes[i], ring_sizes[i]);
        lv_obj_center(rings[i]);
        
        // Configure arc appearance
        lv_arc_set_rotation(rings[i], 270);
        lv_arc_set_bg_angles(rings[i], 0, 360);
        lv_arc_set_range(rings[i], 0, 360);
        lv_arc_set_value(rings[i], 360);
        
        // Style
        lv_obj_set_style_arc_width(rings[i], ring_widths[i], LV_PART_MAIN);
        lv_obj_set_style_arc_width(rings[i], ring_widths[i], LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(rings[i], COLOR_RING_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_color(rings[i], COLOR_RECORDING, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(rings[i], true, LV_PART_INDICATOR);
        lv_obj_remove_style(rings[i], NULL, LV_PART_KNOB);
        lv_obj_clear_flag(rings[i], LV_OBJ_FLAG_CLICKABLE);
        
        // Hidden by default
        lv_obj_add_flag(rings[i], LV_OBJ_FLAG_HIDDEN);
    }
    
    // Create mutex for thread safety
    if (!lvgl_mutex) {
        lvgl_mutex = xSemaphoreCreateMutex();
    }
    
    Serial.println("Status ring: initialized (3 concentric rings)");
}

void status_ring_show(ProcessingState state) {
    if (!rings[0]) return;
    
    // Take mutex for thread safety
    if (lvgl_mutex && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("Status ring: WARNING - could not get mutex for show");
        return;
    }
    
    // Validate all rings
    for (int i = 0; i < STATUS_RING_COUNT; i++) {
        if (!rings[i] || !lv_obj_is_valid(rings[i])) {
            Serial.println("Status ring: WARNING - ring not valid, cannot show");
            if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
            return;
        }
    }
    
    Serial.printf("Status ring: showing state %d\n", state);
    
    current_state = state;
    animation_phase = 0;
    start_time = millis();
    
    // Set color based on state
    lv_color_t color;
    switch (state) {
        case STATE_CONNECTING:
            color = COLOR_CONNECTING;
            break;
        case STATE_RECORDING:
            color = COLOR_RECORDING;
            break;
        case STATE_THINKING:
            color = COLOR_THINKING;
            break;
        case STATE_SPEAKING:
            color = COLOR_SPEAKING;
            break;
        default:
            color = lv_color_hex(0x2E86AB);
            break;
    }
    
    // Apply color and show all rings
    for (int i = 0; i < STATUS_RING_COUNT; i++) {
        lv_obj_set_style_arc_color(rings[i], color, LV_PART_INDICATOR);
        lv_arc_set_rotation(rings[i], 270);
        lv_arc_set_value(rings[i], 360);
        lv_obj_set_style_arc_opa(rings[i], 255, LV_PART_INDICATOR);
        lv_obj_clear_flag(rings[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(rings[i]);
    }
    
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}

void status_ring_hide() {
    // Take mutex for thread safety
    if (lvgl_mutex && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("Status ring: WARNING - could not get mutex for hide");
        current_state = STATE_IDLE;  // Still update state
        return;
    }
    
    current_state = STATE_IDLE;
    
    // First, paint rings black to clear any artifacts
    for (int i = 0; i < STATUS_RING_COUNT; i++) {
        if (rings[i]) {
            // Set to background color and full circle to "erase"
            lv_obj_set_style_arc_color(rings[i], lv_color_hex(0x0F2744), LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(rings[i], 255, LV_PART_INDICATOR);
            lv_arc_set_value(rings[i], 360);
            lv_arc_set_rotation(rings[i], 270);
        }
    }
    
    // Let LVGL render the black rings
    lv_refr_now(NULL);
    
    // Now hide them
    for (int i = 0; i < STATUS_RING_COUNT; i++) {
        if (rings[i]) {
            lv_obj_add_flag(rings[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}

void status_ring_update() {
    if (current_state == STATE_IDLE || !rings[0]) return;
    
    // Take mutex to ensure thread safety with LVGL
    if (!lvgl_mutex || xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;  // Skip this frame if can't get mutex
    }
    
    // Validate rings
    for (int i = 0; i < STATUS_RING_COUNT; i++) {
        if (!rings[i]) {
            Serial.println("Status ring: ring is NULL!");
            xSemaphoreGive(lvgl_mutex);
            return;
        }
        if (!lv_obj_is_valid(rings[i])) {
            Serial.println("Status ring: ring object INVALID!");
            for (int j = 0; j < STATUS_RING_COUNT; j++) rings[j] = NULL;
            current_state = STATE_IDLE;
            xSemaphoreGive(lvgl_mutex);
            return;
        }
    }
    
    // Throttle updates to ~30fps
    static unsigned long last_update = 0;
    unsigned long now = millis();
    if (now - last_update < 33) {
        xSemaphoreGive(lvgl_mutex);
        return;
    }
    last_update = now;
    
    animation_phase += 0.15f;
    if (animation_phase > 2 * M_PI) animation_phase -= 2 * M_PI;
    
    switch (current_state) {
        case STATE_CONNECTING: {
            // Smooth continuous spinning - use millis() for continuous rotation
            unsigned long elapsed = now - start_time;
            int base_rotation = (elapsed / 8) % 360;  // ~45 degrees per second
            
            for (int i = 0; i < STATUS_RING_COUNT; i++) {
                int arc_pos = (base_rotation + i * 40) % 360;  // Offset each ring
                lv_arc_set_value(rings[i], 60 + i * 15);  // Different arc lengths
                lv_arc_set_rotation(rings[i], 270 + arc_pos);
                
                // Staggered opacity pulse
                float pulse = (sinf(animation_phase * 2 + i * 0.5f) + 1.0f) / 2.0f;
                int opa = 150 + (int)(pulse * 105);
                lv_obj_set_style_arc_opa(rings[i], opa, LV_PART_INDICATOR);
            }
            break;
        }
        
        case STATE_RECORDING: {
            // Pulse INWARD effect (opposite of speaking which pulses outward)
            // No spinning! Full circles with ripple effect
            uint8_t level = audio_get_level();
            float audio_factor = level / 100.0f * 4.0f;
            if (audio_factor > 1.0f) audio_factor = 1.0f;
            
            // Progress shown on OUTER ring (visible past finger)
            unsigned long elapsed = now - start_time;
            int progress_angle = min(360UL, elapsed * 360 / 10000);
            if (progress_angle < 10) progress_angle = 10;  // Minimum to avoid display crash
            
            for (int i = 0; i < STATUS_RING_COUNT; i++) {
                // Full circles, no spinning
                lv_arc_set_rotation(rings[i], 270);
                
                // Outer ring shows progress, others full
                if (i == STATUS_RING_COUNT - 1) {
                    lv_arc_set_value(rings[i], progress_angle);
                } else {
                    lv_arc_set_value(rings[i], 360);
                }
                
                // Ripple INWARD - outer rings pulse first, then inner
                // (reverse of speaking which is inner→outer)
                int reverse_i = STATUS_RING_COUNT - 1 - i;  // 2,1,0 instead of 0,1,2
                float phase_offset = reverse_i * 1.5f;
                float pulse = (sinf(animation_phase * 5 - phase_offset) + 1.0f) / 2.0f;
                
                // Audio level boosts the pulse
                int opa = (int)(pulse * 200) + (int)(audio_factor * 55);
                if (opa < 40) opa = 40;  // Minimum visibility
                if (opa > 255) opa = 255;
                lv_obj_set_style_arc_opa(rings[i], opa, LV_PART_INDICATOR);
            }
            break;
        }
        
        case STATE_THINKING: {
            // Same as connecting but yellow - smooth continuous spinning
            unsigned long elapsed = now - start_time;
            int base_rotation = (elapsed / 8) % 360;  // ~45 degrees per second
            
            for (int i = 0; i < STATUS_RING_COUNT; i++) {
                int arc_pos = (base_rotation + i * 40) % 360;  // Offset each ring
                lv_arc_set_value(rings[i], 60 + i * 15);  // Different arc lengths
                lv_arc_set_rotation(rings[i], 270 + arc_pos);
                
                // Staggered opacity pulse
                float pulse = (sinf(animation_phase * 2 + i * 0.5f) + 1.0f) / 2.0f;
                int opa = 150 + (int)(pulse * 105);
                lv_obj_set_style_arc_opa(rings[i], opa, LV_PART_INDICATOR);
            }
            break;
        }
        
        case STATE_SPEAKING: {
            // Ripple outward effect - rings pulse in sequence
            // MORE DRAMATIC: full opacity range, faster, bigger phase offset
            for (int i = 0; i < STATUS_RING_COUNT; i++) {
                lv_arc_set_value(rings[i], 360);
                
                // Each ring pulses at different phase (ripple outward)
                float phase_offset = i * 1.5f;  // Bigger offset = more obvious ripple
                float pulse = (sinf(animation_phase * 5 - phase_offset) + 1.0f) / 2.0f;
                int opa = (int)(pulse * 255);  // Full 0-255 range
                if (opa < 40) opa = 40;  // Minimum visibility
                lv_obj_set_style_arc_opa(rings[i], opa, LV_PART_INDICATOR);
                
                // More obvious rotation wiggle
                int wiggle = (int)(sinf(animation_phase * 6 - phase_offset) * 8);
                lv_arc_set_rotation(rings[i], 270 + wiggle);
            }
            break;
        }
        
        default:
            break;
    }
    
    xSemaphoreGive(lvgl_mutex);
}

ProcessingState status_ring_get_state() {
    return current_state;
}
