/**
 * Avatar state management - swap avatar images based on processing state
 */

#include "avatar.h"
#include "avatar_images.h"
#include "lvgl.h"
#include <Arduino.h>
#include <stdlib.h>

static lv_obj_t* avatar_img = NULL;
static lv_img_dsc_t img_dsc;
static ProcessingState current_avatar_state = STATE_IDLE;

// Image descriptors for each state
static const uint16_t* thinking_images[] = { avatar_thinking_1, avatar_thinking_2 };
static const uint16_t* speaking_images[] = { avatar_speaking_1, avatar_speaking_2 };

void avatar_init(lv_obj_t* parent) {
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
            
        default:
            return;
    }
    
    if (new_image) {
        img_dsc.data = (const uint8_t*)new_image;
        lv_img_set_src(avatar_img, &img_dsc);
        current_avatar_state = state;
        Serial.printf("Avatar: switched to %s\n", state_name);
    }
}

lv_obj_t* avatar_get_obj() {
    return avatar_img;
}
