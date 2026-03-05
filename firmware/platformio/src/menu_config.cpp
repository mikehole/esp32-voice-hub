/**
 * Menu Configuration - Defines all menus and their items
 */

#include "menu_config.h"
#include "avatar_images.h"
#include "avatar_menu_images.h"

//=============================================================================
// MENU DEFINITIONS
//=============================================================================

static const Menu menu_main = {
    .name = "Main",
    .item_count = 8,
    .avatar = avatar_idle,
    .items = {
        {"🎤", "Minerva",  ACTION_VOICE,   0},
        {"🎵", "Music",    ACTION_SUBMENU, MENU_MUSIC},
        {"🏠", "Home",     ACTION_NONE,    0},  // TODO: Home Assistant
        {"☀️", "Weather",  ACTION_NONE,    0},  // TODO
        {"📰", "News",     ACTION_NONE,    0},  // TODO
        {"⏰", "Timer",    ACTION_NONE,    0},  // TODO
        {"📹", "Zoom",     ACTION_SUBMENU, MENU_ZOOM},
        {"⚙️", "Settings", ACTION_SUBMENU, MENU_SETTINGS},
    }
};

static const Menu menu_music = {
    .name = "Music",
    .item_count = 6,
    .avatar = avatar_menu_music,
    .items = {
        {"⏯️", "Play",     ACTION_BLE_MEDIA, MEDIA_PLAY_PAUSE},
        {"⏭️", "Next",     ACTION_BLE_MEDIA, MEDIA_NEXT},
        {"⏮️", "Prev",     ACTION_BLE_MEDIA, MEDIA_PREV},
        {"🔊", "Vol+",     ACTION_BLE_MEDIA, MEDIA_VOL_UP},
        {"🔉", "Vol-",     ACTION_BLE_MEDIA, MEDIA_VOL_DOWN},
        {"🔇", "Mute",     ACTION_BLE_MEDIA, MEDIA_MUTE},
    }
};

static const Menu menu_zoom = {
    .name = "Zoom",
    .item_count = 6,
    .avatar = avatar_menu_zoom,
    .items = {
        {"🔇", "Mute",     ACTION_BLE_KEY, KEY_ZOOM_MUTE},
        {"📹", "Camera",   ACTION_BLE_KEY, KEY_ZOOM_VIDEO},
        {"✋", "Hand",     ACTION_BLE_KEY, KEY_ZOOM_HAND},
        {"🖥️", "Share",    ACTION_BLE_KEY, KEY_ZOOM_SHARE},
        {"💬", "Chat",     ACTION_BLE_KEY, KEY_ZOOM_CHAT},
        {"📞", "Leave",    ACTION_BLE_KEY, KEY_ZOOM_LEAVE},
    }
};

static const Menu menu_settings = {
    .name = "Settings",
    .item_count = 4,
    .avatar = avatar_menu_settings,
    .items = {
        {"🔆", "Bright",   ACTION_NONE, 0},  // TODO: brightness control
        {"📶", "WiFi",     ACTION_NONE, 0},  // TODO: WiFi settings
        {"🔊", "Volume",   ACTION_NONE, 0},  // TODO: volume control
        {"ℹ️", "About",    ACTION_NONE, 0},  // TODO: system info
    }
};

//=============================================================================
// MENU REGISTRY
//=============================================================================

static const Menu* menus[MENU_COUNT] = {
    &menu_main,
    &menu_music,
    &menu_zoom,
    &menu_settings
};

//=============================================================================
// MENU STATE
//=============================================================================

static MenuId current_menu = MENU_MAIN;
static MenuId previous_menu = MENU_MAIN;
static uint8_t selected_index = 0;

//=============================================================================
// MENU FUNCTIONS
//=============================================================================

void menu_init() {
    current_menu = MENU_MAIN;
    previous_menu = MENU_MAIN;
    selected_index = 0;
}

const Menu* menu_get(MenuId id) {
    if (id >= MENU_COUNT) return &menu_main;
    return menus[id];
}

const Menu* menu_get_current() {
    return menus[current_menu];
}

void menu_navigate(MenuId id) {
    if (id >= MENU_COUNT) return;
    previous_menu = current_menu;
    current_menu = id;
    selected_index = 0;
    
    // Update avatar if menu has a themed one
    const Menu* menu = menus[id];
    if (menu->avatar) {
        // TODO: avatar_set_image(menu->avatar);
    }
    
    Serial.printf("Menu: Navigate to %s\n", menu->name);
}

void menu_go_back() {
    if (current_menu == MENU_MAIN) return;  // Already at top
    
    current_menu = MENU_MAIN;  // Always go back to main
    selected_index = 0;
    
    const Menu* menu = menus[current_menu];
    if (menu->avatar) {
        // TODO: avatar_set_image(menu->avatar);
    }
    
    Serial.printf("Menu: Back to %s\n", menu->name);
}

void menu_execute_item(uint8_t index) {
    const Menu* menu = menus[current_menu];
    if (index >= menu->item_count) return;
    
    const MenuItem* item = &menu->items[index];
    Serial.printf("Menu: Execute %s -> %s\n", menu->name, item->label);
    
    switch (item->action) {
        case ACTION_NONE:
            Serial.println("  (no action - placeholder)");
            break;
            
        case ACTION_SUBMENU:
            menu_navigate((MenuId)item->param);
            break;
            
        case ACTION_VOICE:
            Serial.println("  -> Voice assistant");
            // TODO: trigger voice recording
            break;
            
        case ACTION_BLE_KEY:
            Serial.printf("  -> BLE Key: %d\n", item->param);
            // TODO: send BLE HID keystroke
            break;
            
        case ACTION_BLE_MEDIA:
            Serial.printf("  -> BLE Media: %d\n", item->param);
            // TODO: send BLE HID media key
            break;
            
        case ACTION_CALLBACK:
            Serial.println("  -> Callback");
            // TODO: invoke callback
            break;
    }
}

uint8_t menu_get_selected() {
    return selected_index;
}

void menu_set_selected(uint8_t index) {
    const Menu* menu = menus[current_menu];
    if (index < menu->item_count) {
        selected_index = index;
    }
}

void menu_rotate(int8_t direction) {
    const Menu* menu = menus[current_menu];
    int8_t new_index = (int8_t)selected_index + direction;
    
    // Wrap around
    if (new_index < 0) {
        new_index = menu->item_count - 1;
    } else if (new_index >= menu->item_count) {
        new_index = 0;
    }
    
    selected_index = new_index;
}

bool menu_is_top_level() {
    return current_menu == MENU_MAIN;
}
