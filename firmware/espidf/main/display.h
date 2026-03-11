#pragma once

#include <stdbool.h>
#include <stdint.h>

// Display states
typedef enum {
    DISPLAY_STATE_IDLE,
    DISPLAY_STATE_CONNECTING,
    DISPLAY_STATE_LISTENING,
    DISPLAY_STATE_THINKING,
    DISPLAY_STATE_SPEAKING,
    DISPLAY_STATE_ERROR
} display_state_t;

// Initialize display (LCD + LVGL)
void display_init(void);

// Call from main loop to handle LVGL tasks (no-op, runs in own task)
void display_loop(void);

// Set current display state (updates UI)
void display_set_state(display_state_t state);

// Show a notification message
void display_show_notification(const char* title, const char* message);

// Set backlight brightness (0-255)
void display_set_brightness(uint8_t brightness);

// Get current brightness (0-255)
uint8_t display_get_brightness(void);

// Lock/unlock LVGL mutex for thread-safe access
bool display_lock(int timeout_ms);
void display_unlock(void);

// Screenshot capture - call start, wait for complete, get buffer
bool display_screenshot_start(void);
bool display_screenshot_complete(void);
uint16_t* display_screenshot_get_buffer(void);
void display_screenshot_free(void);
