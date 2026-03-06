/**
 * Avatar state management - swap avatar images based on processing state or selected wedge
 */

#include "avatar.h"
#include "avatar_images.h"
#include "avatar_menu_images.h"
#include "avatar_notification.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include <Arduino.h>
#include <stdlib.h>

static lv_obj_t* avatar_img = NULL;
static lv_img_dsc_t img_dsc;
static ProcessingState current_avatar_state = STATE_IDLE;
static int current_wedge = 0;

// Image descriptors for each state
static const uint16_t* thinking_images[] = { avatar_thinking_1, avatar_thinking_2 };
static const uint16_t* speaking_images[] = { avatar_speaking_1, avatar_speaking_2 };

// Menu wedge avatars (matches wedge order)
static const uint16_t* wedge_avatars[] = {
    avatar_idle,           // 0: Minerva
    avatar_menu_music,     // 1: Music
    avatar_menu_home,      // 2: Home
    avatar_menu_weather,   // 3: Weather
    avatar_menu_news,      // 4: News
    avatar_menu_timer,     // 5: Timer
    avatar_menu_zoom,      // 6: Zoom
    avatar_menu_settings,  // 7: Settings
};

void avatar_init(lv_obj_t* parent) {
    // NOTE: Caller must hold LVGL mutex! (called from create_radial_ui)
    
    // Create image object
    avatar_img = lv_img_create(parent);
    lv_obj_center(avatar_img);
    
    // Set up image descriptor template
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.header.always_zero = 0;
    img_dsc.header.reserved = 0;
    img_dsc.header.w = 130;
    img_dsc.header.h = 130;
    img_dsc.data_size = 130 * 130 * 2;  // RGB565 = 2 bytes per pixel
    img_dsc.data = (const uint8_t*)avatar_idle;
    
    lv_img_set_src(avatar_img, &img_dsc);
    
    Serial.println("Avatar: initialized with idle image");
}

void avatar_set_state(ProcessingState state) {
    if (!avatar_img) return;
    if (state == current_avatar_state) return;
    
    const uint16_t* new_image = NULL;
    const char* state_name = "";
    
    switch (state) {
        case STATE_IDLE:
            new_image = avatar_idle;
            state_name = "idle";
            break;
            
        case STATE_CONNECTING:
            new_image = avatar_connecting;
            state_name = "connecting (zapped!)";
            break;
            
        case STATE_THINKING:
            // Random choice between two thinking images
            new_image = thinking_images[random(2)];
            state_name = "thinking";
            break;
            
        case STATE_SPEAKING:
            // Random choice between two speaking images
            new_image = speaking_images[random(2)];
            state_name = "speaking";
            break;
            
        case STATE_RECORDING:
            // Listening pose - hand cupped to ear
            new_image = avatar_recording;
            state_name = "recording (listening)";
            break;
            
        case STATE_NOTIFICATION:
            // Tapping screen pose - notification pending
            new_image = avatar_notification;
            state_name = "notification (tap me!)";
            break;
            
        default:
            return;
    }
    
    if (new_image) {
        if (lvgl_port_lock(50)) {
            img_dsc.data = (const uint8_t*)new_image;
            lv_img_set_src(avatar_img, &img_dsc);
            // Force redraw of avatar area
            lv_obj_invalidate(avatar_img);
            lvgl_port_unlock();
        }
        current_avatar_state = state;
        Serial.printf("Avatar: switched to %s\n", state_name);
    }
}

lv_obj_t* avatar_get_obj() {
    return avatar_img;
}

// Set avatar based on selected wedge (for menu navigation)
void avatar_set_wedge(int wedge_index) {
    if (!avatar_img) return;
    if (wedge_index < 0 || wedge_index > 7) return;
    if (wedge_index == current_wedge && current_avatar_state == STATE_IDLE) return;
    
    const uint16_t* new_image = wedge_avatars[wedge_index];
    
    if (lvgl_port_lock(50)) {
        img_dsc.data = (const uint8_t*)new_image;
        lv_img_set_src(avatar_img, &img_dsc);
        // Force redraw of avatar area
        lv_obj_invalidate(avatar_img);
        lvgl_port_unlock();
    }
    
    current_wedge = wedge_index;
    current_avatar_state = STATE_IDLE;  // Reset state when changing wedge
    
    Serial.printf("Avatar: wedge %d selected\n", wedge_index);
}

// Custom avatar support
static uint16_t* custom_avatar_buffer = NULL;
static bool custom_avatar_active = false;

bool avatar_set_custom(const uint16_t* rgb565_data, size_t size) {
    if (!avatar_img) return false;
    
    // Avatar is 130x130 RGB565 = 33800 bytes
    const size_t expected_size = 130 * 130 * 2;
    if (size != expected_size) {
        Serial.printf("Avatar: custom image wrong size (%u, expected %u)\n", size, expected_size);
        return false;
    }
    
    // Allocate buffer in PSRAM if not already
    if (!custom_avatar_buffer) {
        custom_avatar_buffer = (uint16_t*)heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM);
        if (!custom_avatar_buffer) {
            Serial.println("Avatar: failed to allocate custom buffer");
            return false;
        }
    }
    
    // Copy data
    memcpy(custom_avatar_buffer, rgb565_data, expected_size);
    
    // Display it
    if (lvgl_port_lock(50)) {
        img_dsc.data = (const uint8_t*)custom_avatar_buffer;
        lv_img_set_src(avatar_img, &img_dsc);
        lv_obj_invalidate(avatar_img);
        lvgl_port_unlock();
    }
    
    custom_avatar_active = true;
    Serial.println("Avatar: custom image set");
    return true;
}

bool avatar_is_custom() {
    return custom_avatar_active;
}

void avatar_reset_custom() {
    if (!custom_avatar_active) return;
    
    custom_avatar_active = false;
    
    // Return to current wedge avatar
    avatar_set_wedge(current_wedge);
    Serial.println("Avatar: reset to normal");
}
