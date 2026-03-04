/**
 * Menu Configuration System
 * 
 * Defines the hierarchical menu structure with support for:
 * - Variable item counts (2, 4, 6, 8 wedges)
 * - Submenus with themed avatars
 * - BLE HID actions (keyboard shortcuts, media keys)
 * - Voice assistant integration
 */

#ifndef MENU_CONFIG_H
#define MENU_CONFIG_H

#include <Arduino.h>

// Menu IDs
enum MenuId {
    MENU_MAIN = 0,
    MENU_MUSIC,
    MENU_ZOOM,
    MENU_SETTINGS,
    MENU_COUNT
};

// Action types for menu items
enum ActionType {
    ACTION_NONE = 0,        // Placeholder / disabled
    ACTION_SUBMENU,         // Navigate to another menu
    ACTION_VOICE,           // Minerva voice assistant
    ACTION_BLE_KEY,         // Send BLE HID keystroke (e.g., Alt+A)
    ACTION_BLE_MEDIA,       // Send BLE HID media key (play/pause, vol)
    ACTION_CALLBACK         // Custom callback function
};

// BLE Media key codes
enum MediaKey {
    MEDIA_PLAY_PAUSE = 0,
    MEDIA_NEXT,
    MEDIA_PREV,
    MEDIA_VOL_UP,
    MEDIA_VOL_DOWN,
    MEDIA_MUTE
};

// Zoom shortcut codes (will map to actual key combos)
enum ZoomKey {
    KEY_ZOOM_MUTE = 0,      // Alt+A
    KEY_ZOOM_VIDEO,         // Alt+V
    KEY_ZOOM_HAND,          // Alt+Y
    KEY_ZOOM_SHARE,         // Alt+S
    KEY_ZOOM_CHAT,          // Alt+H
    KEY_ZOOM_LEAVE          // Alt+Q
};

// Menu item definition
struct MenuItem {
    const char* icon;       // Emoji or icon code
    const char* label;      // Display text
    ActionType action;      // What happens when selected
    uint8_t param;          // Action parameter (submenu_id, keycode, etc.)
};

// Menu definition
struct Menu {
    const char* name;           // Menu name (for debugging)
    uint8_t item_count;         // Number of items: 2, 4, 6, or 8
    const uint16_t* avatar;     // Themed avatar (NULL = keep current)
    MenuItem items[8];          // Menu items (max 8)
};

// Menu system functions
void menu_init();
const Menu* menu_get(MenuId id);
const Menu* menu_get_current();
void menu_navigate(MenuId id);
void menu_go_back();
void menu_execute_item(uint8_t index);
uint8_t menu_get_selected();
void menu_set_selected(uint8_t index);
void menu_rotate(int8_t direction);
bool menu_is_top_level();

#endif // MENU_CONFIG_H
