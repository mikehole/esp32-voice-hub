#pragma once

// Initialize display (LCD + LVGL)
void display_init(void);

// Call from main loop to handle LVGL tasks
void display_loop(void);

// Display states
typedef enum {
    DISPLAY_STATE_IDLE,
    DISPLAY_STATE_CONNECTING,
    DISPLAY_STATE_LISTENING,
    DISPLAY_STATE_THINKING,
    DISPLAY_STATE_SPEAKING,
    DISPLAY_STATE_ERROR
} display_state_t;

// Set current display state (updates avatar/ring)
void display_set_state(display_state_t state);

// Show a notification message
void display_show_notification(const char* title, const char* message);
