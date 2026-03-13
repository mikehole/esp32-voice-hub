/**
 * Wedge UI - Radial menu with avatar center
 * 
 * 8 wedge segments around a center circle containing
 * the Minerva avatar with state animations.
 * 
 * Supports nested menus (main menu -> settings submenu etc)
 */

#ifndef WEDGE_UI_H
#define WEDGE_UI_H

#include <stdbool.h>

// Avatar states (matches display states)
typedef enum {
    AVATAR_IDLE = 0,
    AVATAR_LISTENING,
    AVATAR_THINKING,
    AVATAR_SPEAKING,
    AVATAR_NOTIFICATION
} avatar_state_t;

// Menu IDs
typedef enum {
    MENU_MAIN = 0,
    MENU_SETTINGS,
    MENU_MUSIC,      // Music control mode
    MENU_ZOOM,       // Zoom control mode
} menu_id_t;

// Action result from center tap
typedef enum {
    ACTION_NONE = 0,
    ACTION_VOICE_START,      // Start voice recording
    ACTION_SUBMENU,          // Entered a submenu
    ACTION_OTA_MODE,         // Enter OTA mode (pause wakeword)
    ACTION_OTA_CHECK,        // Check for updates
    ACTION_OTA_INSTALL,      // Install available update
    ACTION_TOGGLE_WAKEWORD,  // Toggle wake word on/off
    ACTION_BACK,             // Go back to parent menu
} wedge_action_t;

// Initialize wedge UI (call after LVGL init)
bool wedge_ui_init(void);

// Set avatar state (updates center image)
void wedge_ui_set_avatar_state(avatar_state_t state);

// Set selected wedge (0-7, highlights that segment)
void wedge_ui_set_selection(int wedge);

// Get current selection
int wedge_ui_get_selection(void);

// Rotate selection (delta = +1 or -1)
void wedge_ui_rotate(int delta);

// Handle center tap - returns action to perform
wedge_action_t wedge_ui_center_tap(void);

// Get current menu
menu_id_t wedge_ui_get_menu(void);

// Check if in OTA mode
bool wedge_ui_is_ota_mode(void);

// Exit OTA mode (call after OTA complete or timeout)
void wedge_ui_exit_ota_mode(void);

// Set update availability (called after update check completes)
void wedge_ui_set_update_available(bool available);

// Reset OTA state to idle
void wedge_ui_reset_ota_state(void);

// Adjustment mode (for brightness, volume, etc.)
bool wedge_ui_is_adjusting(void);
void wedge_ui_adjust_value(int delta);  // +/- percentage points

#endif // WEDGE_UI_H
