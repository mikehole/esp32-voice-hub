/**
 * Status Ring - Animated ring around avatar for visual feedback
 */

#include "status_ring.h"
#include "audio_capture.h"
#include <math.h>

// Colors
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
    // Create arc that surrounds the avatar
    ring = lv_arc_create(parent);
    lv_obj_set_size(ring, 155, 155);  // Larger than 130px avatar
    lv_obj_center(ring);
    
    // Configure arc appearance
    lv_arc_set_rotation(ring, 270);  // Start from top
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_range(ring, 0, 360);
    lv_arc_set_value(ring, 360);
    
    // Style
    lv_obj_set_style_arc_width(ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, COLOR_RING_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, COLOR_RECORDING, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    
    // Hidden by default
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
}

void status_ring_show(ProcessingState state) {
    if (!ring) return;
    
    current_state = state;
    animation_phase = 0;
    start_time = millis();
    
    // Set color based on state
    lv_color_t color;
    switch (state) {
        case STATE_RECORDING:
            color = COLOR_RECORDING;
            lv_arc_set_value(ring, 0);  // Start empty, fill over time
            break;
        case STATE_THINKING:
            color = COLOR_THINKING;
            lv_arc_set_value(ring, 360);  // Full circle
            break;
        case STATE_SPEAKING:
            color = COLOR_SPEAKING;
            lv_arc_set_value(ring, 360);  // Full circle
            break;
        default:
            color = lv_color_hex(0x2E86AB);
            break;
    }
    
    lv_obj_set_style_arc_color(ring, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring, 8, LV_PART_INDICATOR);
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
    if (current_state == STATE_IDLE || !ring) return;
    
    // Throttle updates to ~30fps
    static unsigned long last_update = 0;
    if (millis() - last_update < 33) return;
    last_update = millis();
    
    animation_phase += 0.15f;
    if (animation_phase > 2 * M_PI) animation_phase -= 2 * M_PI;
    
    switch (current_state) {
        case STATE_RECORDING: {
            // Pulsing width based on audio level
            uint8_t level = audio_get_level();
            float factor = level / 100.0f * 4.0f;
            if (factor > 1.0f) factor = 1.0f;
            
            // Sine wave pulse + audio level
            float pulse = (sinf(animation_phase * 3) + 1.0f) / 2.0f;
            int width = 6 + (int)(pulse * 4) + (int)(factor * 8);
            lv_obj_set_style_arc_width(ring, width, LV_PART_INDICATOR);
            
            // Fill arc over 10 seconds
            unsigned long elapsed = millis() - start_time;
            int angle = min(360UL, elapsed * 360 / 10000);
            lv_arc_set_value(ring, angle);
            break;
        }
        
        case STATE_THINKING: {
            // Smooth pulsing glow
            float pulse = (sinf(animation_phase * 2) + 1.0f) / 2.0f;
            int width = 5 + (int)(pulse * 10);
            lv_obj_set_style_arc_width(ring, width, LV_PART_INDICATOR);
            
            // Rotate for spinning effect
            int rotation = (int)(animation_phase * 50) % 360;
            lv_arc_set_rotation(ring, 270 + rotation);
            break;
        }
        
        case STATE_SPEAKING: {
            // Wiggle/bounce effect
            float wiggle = sinf(animation_phase * 8);
            float bounce = (sinf(animation_phase * 4) + 1.0f) / 2.0f;
            
            int width = 6 + (int)(bounce * 6);
            lv_obj_set_style_arc_width(ring, width, LV_PART_INDICATOR);
            
            // Subtle rotation wiggle
            int wiggle_rot = (int)(wiggle * 8);
            lv_arc_set_rotation(ring, 270 + wiggle_rot);
            break;
        }
        
        default:
            break;
    }
}

ProcessingState status_ring_get_state() {
    return current_state;
}
