/**
 * Status Ring - Animated ring around avatar for visual feedback
 */

#include "status_ring.h"
#include "audio_capture.h"
#include <math.h>

// Colors
#define COLOR_CONNECTING   lv_color_hex(0x3498DB)  // Blue for connecting
#define COLOR_RECORDING    lv_color_hex(0xE74C3C)  // Red
#define COLOR_THINKING     lv_color_hex(0xF39C12)  // Orange  
#define COLOR_SPEAKING     lv_color_hex(0x2ECC71)  // Green
#define COLOR_RING_BG      lv_color_hex(0x1A1A1A)  // Dark background

// Ring object
static lv_obj_t* ring = NULL;
static ProcessingState current_state = STATE_IDLE;
static float animation_phase = 0;
static unsigned long start_time = 0;

void status_ring_init(lv_obj_t* parent) {
    if (!parent) {
        Serial.println("Status ring: ERROR - null parent!");
        return;
    }
    
    // Create arc that surrounds the avatar
    ring = lv_arc_create(parent);
    if (!ring) {
        Serial.println("Status ring: ERROR - failed to create arc!");
        return;
    }
    Serial.println("Status ring: initialized");
    lv_obj_set_size(ring, 155, 155);  // Larger than 130px avatar
    lv_obj_center(ring);
    
    // Configure arc appearance
    lv_arc_set_rotation(ring, 270);  // Start from top
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_range(ring, 0, 360);
    lv_arc_set_value(ring, 360);
    
    // Style - fixed width, we'll animate other properties
    lv_obj_set_style_arc_width(ring, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, COLOR_RING_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, COLOR_RECORDING, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    
    // Hidden by default
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
}

void status_ring_show(ProcessingState state) {
    if (!ring || !lv_obj_is_valid(ring)) {
        Serial.println("Status ring: WARNING - ring not valid, cannot show");
        return;
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
            lv_arc_set_value(ring, 0);  // Start empty, fill over time
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
    
    lv_obj_set_style_arc_color(ring, color, LV_PART_INDICATOR);
    lv_arc_set_rotation(ring, 270);
    lv_arc_set_bg_angles(ring, 0, 360);
    
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ring);
}

void status_ring_hide() {
    if (!ring) return;
    
    current_state = STATE_IDLE;
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
}

void status_ring_update() {
    // Safety: bail early if not active or ring doesn't exist
    if (current_state == STATE_IDLE) return;
    if (!ring || !lv_obj_is_valid(ring)) return;
    
    // Throttle updates to ~30fps
    static unsigned long last_update = 0;
    unsigned long now = millis();
    if (now - last_update < 33) return;
    last_update = now;
    
    animation_phase += 0.15f;
    if (animation_phase > 2 * M_PI) animation_phase -= 2 * M_PI;
    
    switch (current_state) {
        case STATE_CONNECTING: {
            // Spinning arc segment (like a loading spinner)
            int arc_pos = ((int)(animation_phase * 40)) % 360;
            lv_arc_set_value(ring, 60);  // 60 degree arc segment
            lv_arc_set_rotation(ring, 270 + arc_pos);
            
            // Gentle pulse
            float pulse = (sinf(animation_phase * 2) + 1.0f) / 2.0f;
            int opa = 180 + (int)(pulse * 75);
            lv_obj_set_style_arc_opa(ring, opa, LV_PART_INDICATOR);
            break;
        }
        
        case STATE_RECORDING: {
            // Fill arc over 10 seconds (this works!)
            unsigned long elapsed = now - start_time;
            int angle = min(360UL, elapsed * 360 / 10000);
            lv_arc_set_value(ring, angle);
            
            // Pulse opacity based on audio level
            uint8_t level = audio_get_level();
            float factor = level / 100.0f * 4.0f;
            if (factor > 1.0f) factor = 1.0f;
            float pulse = (sinf(animation_phase * 3) + 1.0f) / 2.0f;
            int opa = 180 + (int)(pulse * 50) + (int)(factor * 25);
            if (opa > 255) opa = 255;
            lv_obj_set_style_arc_opa(ring, opa, LV_PART_INDICATOR);
            break;
        }
        
        case STATE_THINKING: {
            // Spinning arc segment (not full circle)
            // Use angles directly for spinning effect
            int arc_start = ((int)(animation_phase * 30)) % 360;
            int arc_end = (arc_start + 90) % 360;  // 90 degree segment
            
            // Set as indicator angles
            lv_arc_set_value(ring, 90);  // 90 degree arc
            lv_arc_set_rotation(ring, 270 + arc_start);  // Rotate the whole arc
            
            // Pulse opacity
            float pulse = (sinf(animation_phase * 2) + 1.0f) / 2.0f;
            int opa = 150 + (int)(pulse * 105);
            lv_obj_set_style_arc_opa(ring, opa, LV_PART_INDICATOR);
            break;
        }
        
        case STATE_SPEAKING: {
            // Full circle with wiggling opacity
            lv_arc_set_value(ring, 360);
            
            // Fast wiggle on opacity
            float wiggle = sinf(animation_phase * 8);
            int opa = 200 + (int)(wiggle * 55);
            lv_obj_set_style_arc_opa(ring, opa, LV_PART_INDICATOR);
            
            // Subtle position wiggle via rotation
            int rot_wiggle = (int)(sinf(animation_phase * 6) * 5);
            lv_arc_set_rotation(ring, 270 + rot_wiggle);
            break;
        }
        
        default:
            break;
    }
}

ProcessingState status_ring_get_state() {
    return current_state;
}
