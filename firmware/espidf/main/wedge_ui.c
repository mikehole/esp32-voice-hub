/**
 * Wedge UI - Radial menu with avatar center
 */

#include "wedge_ui.h"
#include "avatar_images.h"
#include "avatar_menu_images.h"
#include "display.h"
#include "update_checker.h"
#include "wakeword.h"
#include "command_server.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"

static const char *TAG = "wedge_ui";

// Display dimensions (matching PlatformIO)
#define SCREEN_SIZE     360
#define CENTER_X        (SCREEN_SIZE / 2)
#define CENTER_Y        (SCREEN_SIZE / 2)
#define OUTER_RADIUS    165
#define INNER_RADIUS    85
#define AVATAR_SIZE     130

// Colors (matching PlatformIO)
#define COLOR_BG        lv_color_black()
#define COLOR_WEDGE     lv_color_hex(0x1a1a2e)
#define COLOR_WEDGE_ALT lv_color_hex(0x16213e)
#define COLOR_SELECTED  lv_color_hex(0x5DADE2)  // Cyan highlight
#define COLOR_CENTER    lv_color_hex(0x1a1a2e)
#define COLOR_BORDER    lv_color_hex(0x533483)
#define COLOR_TEXT      lv_color_hex(0x5DADE2)  // Cyan text

// Main menu labels
static const char* main_menu_labels[] = {
    "Minerva",   // 0: Top - voice assistant
    "Music",     // 1
    "Home",      // 2
    "Weather",   // 3
    "News",      // 4
    "Timer",     // 5
    "Zoom",      // 6
    "Settings"   // 7 - opens settings submenu
};

// Settings submenu labels
static const char* settings_menu_labels[] = {
    "< Back",       // 0: Return to main menu
    "OTA",          // 1: Pause wakeword for OTA
    "Wake",         // 2: Toggle wake word on/off
    "Bright",       // 3: Adjust brightness
    "Volume",       // 4: Adjust volume
    "WiFi",         // 5: WiFi settings
    "BT",           // 6: Bluetooth pairing
    "Restart"       // 7: Reboot device
};

// Music control menu labels
static const char* music_menu_labels[] = {
    "< Back",       // 0: Return to main menu
    "|<<",          // 1: Previous track
    ">>|",          // 2: Next track  
    "Play",         // 3: Play/Pause toggle
    "Vol-",         // 4: Volume down
    "Vol+",         // 5: Volume up
    "Mute",         // 6: Mute toggle
    "Spotify"       // 7: Launch/focus Spotify
};

// Current menu state
static menu_id_t current_menu = MENU_MAIN;
static const char** current_labels = main_menu_labels;
static bool ota_mode = false;
static bool wakeword_enabled = true;

// OTA update state
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_UPDATE_AVAILABLE,
    OTA_STATE_NO_UPDATE,
    OTA_STATE_INSTALLING,
} ota_state_t;

static ota_state_t ota_state = OTA_STATE_IDLE;

// Adjustment mode state
typedef enum {
    ADJUST_NONE = 0,
    ADJUST_BRIGHTNESS,
    ADJUST_VOLUME,
} adjust_mode_t;

static adjust_mode_t adjust_mode = ADJUST_NONE;
static int adjust_value = 50;  // Current value being adjusted (0-100)
static lv_obj_t* adjust_arc = NULL;  // Arc indicator for adjustment

// State
static int selected_wedge = 0;
static avatar_state_t current_avatar_state = AVATAR_IDLE;
static bool ui_initialized = false;

// LVGL objects
static lv_obj_t* highlight_meter = NULL;
static lv_meter_scale_t* highlight_scale = NULL;
static lv_meter_indicator_t* highlight_arc = NULL;
static lv_obj_t* center_obj = NULL;
static lv_obj_t* avatar_img = NULL;
static lv_obj_t* center_text = NULL;  // Text shown in settings mode
static lv_obj_t* wedge_labels_obj[8] = {0};

// Avatar image descriptors
static lv_img_dsc_t avatar_idle_dsc;
static lv_img_dsc_t avatar_listening_dsc;
static lv_img_dsc_t avatar_thinking_dsc;
static lv_img_dsc_t avatar_speaking_dsc;
static lv_img_dsc_t avatar_notification_dsc;

// Menu avatars for each wedge
static lv_img_dsc_t avatar_menu_dsc[7];  // 7 menu avatars (wedges 1-7)

// External LVGL mutex (from display.c)
extern SemaphoreHandle_t lvgl_mutex;

static void init_avatar_descriptors(void) {
    // Initialize image descriptors for LVGL
    avatar_idle_dsc.header.always_zero = 0;
    avatar_idle_dsc.header.w = AVATAR_SIZE;
    avatar_idle_dsc.header.h = AVATAR_SIZE;
    avatar_idle_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_idle_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_idle_dsc.data = (const uint8_t*)avatar_idle;
    
    avatar_listening_dsc.header.always_zero = 0;
    avatar_listening_dsc.header.w = AVATAR_SIZE;
    avatar_listening_dsc.header.h = AVATAR_SIZE;
    avatar_listening_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_listening_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_listening_dsc.data = (const uint8_t*)avatar_recording;  // Use recording avatar
    
    avatar_thinking_dsc.header.always_zero = 0;
    avatar_thinking_dsc.header.w = AVATAR_SIZE;
    avatar_thinking_dsc.header.h = AVATAR_SIZE;
    avatar_thinking_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_thinking_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_thinking_dsc.data = (const uint8_t*)avatar_thinking_1;  // Use thinking_1
    
    avatar_speaking_dsc.header.always_zero = 0;
    avatar_speaking_dsc.header.w = AVATAR_SIZE;
    avatar_speaking_dsc.header.h = AVATAR_SIZE;
    avatar_speaking_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_speaking_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_speaking_dsc.data = (const uint8_t*)avatar_speaking_1;
    
    avatar_notification_dsc.header.always_zero = 0;
    avatar_notification_dsc.header.w = AVATAR_SIZE;
    avatar_notification_dsc.header.h = AVATAR_SIZE;
    avatar_notification_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    avatar_notification_dsc.data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
    avatar_notification_dsc.data = (const uint8_t*)avatar_notification;
    
    // Initialize menu avatars (one per wedge 1-7)
    const uint16_t* menu_images[] = {
        avatar_menu_music,     // wedge 1
        avatar_menu_home,      // wedge 2
        avatar_menu_weather,   // wedge 3
        avatar_menu_news,      // wedge 4
        avatar_menu_timer,     // wedge 5
        avatar_menu_zoom,      // wedge 6 (Lights in labels)
        avatar_menu_settings,  // wedge 7
    };
    
    for (int i = 0; i < 7; i++) {
        avatar_menu_dsc[i].header.always_zero = 0;
        avatar_menu_dsc[i].header.w = AVATAR_SIZE;
        avatar_menu_dsc[i].header.h = AVATAR_SIZE;
        avatar_menu_dsc[i].header.cf = LV_IMG_CF_TRUE_COLOR;
        avatar_menu_dsc[i].data_size = AVATAR_SIZE * AVATAR_SIZE * 2;
        avatar_menu_dsc[i].data = (const uint8_t*)menu_images[i];
    }
}

// Forward declarations
static void update_center_content(void);
static void enter_adjustment_mode(adjust_mode_t mode, int initial_value);
static void exit_adjustment_mode(void);

// Update center content based on menu and selection
static void update_center_content(void) {
    if (!center_text || !avatar_img) return;
    
    if (current_menu == MENU_MAIN) {
        // Main menu - show avatar, hide text
        lv_obj_clear_flag(avatar_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(center_text, LV_OBJ_FLAG_HIDDEN);
        
        // Update avatar based on selected wedge
        if (selected_wedge == 0) {
            lv_img_set_src(avatar_img, &avatar_idle_dsc);
        } else {
            lv_img_set_src(avatar_img, &avatar_menu_dsc[selected_wedge - 1]);
        }
    } else if (current_menu == MENU_SETTINGS) {
        // Settings menu - show text, hide avatar
        lv_obj_add_flag(avatar_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(center_text, LV_OBJ_FLAG_HIDDEN);
        
        // Set text based on selected setting
        const char* text = "";
        switch (selected_wedge) {
            case 0: text = "Tap to\ngo back"; break;
            case 1:  // OTA - show state
                switch (ota_state) {
                    case OTA_STATE_CHECKING: text = "Checking\n..."; break;
                    case OTA_STATE_UPDATE_AVAILABLE: text = "Update\navailable!"; break;
                    case OTA_STATE_NO_UPDATE: text = "Up to\ndate"; break;
                    case OTA_STATE_INSTALLING: text = "Installing\n..."; break;
                    default: text = "Check for\nupdates"; break;
                }
                break;
            case 2: text = wakeword_enabled ? "Wake Word\nON" : "Wake Word\nOFF"; break;
            case 3: text = "Adjust\nbrightness"; break;
            case 4: text = "Adjust\nvolume"; break;
            case 5: text = "WiFi\nsetup"; break;
            case 6: text = "(unused)"; break;
            case 7: text = "Tap to\nrestart"; break;
        }
        lv_label_set_text(center_text, text);
    } else if (current_menu == MENU_MUSIC) {
        // Music menu - show text with music controls info
        lv_obj_add_flag(avatar_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(center_text, LV_OBJ_FLAG_HIDDEN);
        
        const char* text = "";
        int clients = command_server_client_count();
        
        if (clients == 0) {
            text = "No PC\nconnected";
        } else {
            switch (selected_wedge) {
                case 0: text = "Tap to\nexit"; break;
                case 1: text = "Previous\ntrack"; break;
                case 2: text = "Next\ntrack"; break;
                case 3: text = "Play /\nPause"; break;
                case 4: text = "Volume\ndown"; break;
                case 5: text = "Volume\nup"; break;
                case 6: text = "Mute"; break;
                case 7: text = "Open\nSpotify"; break;
                default: text = "Music\nControl"; break;
            }
        }
        lv_label_set_text(center_text, text);
    }
}

static void update_highlight(void) {
    if (!highlight_meter || !highlight_arc) return;
    
    int start = selected_wedge * 45;
    int end = start + 43;
    lv_meter_set_indicator_start_value(highlight_meter, highlight_arc, start);
    lv_meter_set_indicator_end_value(highlight_meter, highlight_arc, end);
    
    // Update label colors - selected wedge gets dark text (on light bg), others get cyan
    for (int i = 0; i < 8; i++) {
        if (wedge_labels_obj[i]) {
            lv_color_t color = (i == selected_wedge) ? lv_color_hex(0x0a1929) : COLOR_TEXT;
            lv_obj_set_style_text_color(wedge_labels_obj[i], color, 0);
        }
    }
    
    // Update center content
    update_center_content();
}

bool wedge_ui_init(void) {
    ESP_LOGI(TAG, "Initializing wedge UI...");
    
    init_avatar_descriptors();
    
    lv_obj_t* screen = lv_scr_act();
    
    // Black background
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    
    // Create base meter for wedges
    lv_obj_t* meter = lv_meter_create(screen);
    lv_obj_set_size(meter, OUTER_RADIUS * 2, OUTER_RADIUS * 2);
    lv_obj_center(meter);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter, 0, 0);
    lv_obj_set_style_pad_all(meter, 0, 0);
    
    // Add scale
    lv_meter_scale_t* scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_range(meter, scale, 0, 360, 360, 270);
    lv_meter_set_scale_ticks(meter, scale, 0, 0, 0, lv_color_black());
    
    // Draw 8 arc segments
    for (int i = 0; i < 8; i++) {
        int start = i * 45;
        int end = start + 43;
        lv_color_t color = (i % 2 == 0) ? COLOR_WEDGE : COLOR_WEDGE_ALT;
        
        lv_meter_indicator_t* indic = lv_meter_add_arc(meter, scale, 
            OUTER_RADIUS - INNER_RADIUS, color, 0);
        lv_meter_set_indicator_start_value(meter, indic, start);
        lv_meter_set_indicator_end_value(meter, indic, end);
    }
    
    // Create highlight meter (overlays selected wedge)
    highlight_meter = lv_meter_create(screen);
    lv_obj_set_size(highlight_meter, OUTER_RADIUS * 2, OUTER_RADIUS * 2);
    lv_obj_center(highlight_meter);
    lv_obj_set_style_bg_opa(highlight_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(highlight_meter, 0, 0);
    lv_obj_set_style_pad_all(highlight_meter, 0, 0);
    
    highlight_scale = lv_meter_add_scale(highlight_meter);
    lv_meter_set_scale_range(highlight_meter, highlight_scale, 0, 360, 360, 270);
    lv_meter_set_scale_ticks(highlight_meter, highlight_scale, 0, 0, 0, lv_color_black());
    
    // Create highlight arc
    highlight_arc = lv_meter_add_arc(highlight_meter, highlight_scale, 
        OUTER_RADIUS - INNER_RADIUS, COLOR_SELECTED, 0);
    
    // Add labels for each wedge
    for (int i = 0; i < 8; i++) {
        float angle_deg = i * 45 + 22.5 - 90;
        float angle_rad = angle_deg * M_PI / 180.0;
        float label_radius = (OUTER_RADIUS + INNER_RADIUS) / 2;
        
        int label_x = CENTER_X + (int)(cos(angle_rad) * label_radius) - 22;
        int label_y = CENTER_Y + (int)(sin(angle_rad) * label_radius) - 8;
        
        lv_obj_t* label = lv_label_create(screen);
        lv_label_set_text(label, current_labels[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT, 0);  // Cyan by default
        lv_obj_set_pos(label, label_x, label_y);
        wedge_labels_obj[i] = label;
    }
    
    // Center circle
    center_obj = lv_obj_create(screen);
    lv_obj_set_size(center_obj, AVATAR_SIZE, AVATAR_SIZE);  // Exact size, no padding
    lv_obj_center(center_obj);
    lv_obj_set_style_radius(center_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_obj, COLOR_CENTER, 0);
    lv_obj_set_style_border_color(center_obj, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(center_obj, 3, 0);
    lv_obj_set_style_clip_corner(center_obj, true, 0);
    lv_obj_clear_flag(center_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Avatar image in center
    avatar_img = lv_img_create(center_obj);
    lv_img_set_src(avatar_img, &avatar_idle_dsc);
    lv_obj_center(avatar_img);
    
    // Center text (hidden by default, shown in settings mode)
    center_text = lv_label_create(center_obj);
    lv_label_set_text(center_text, "");
    lv_obj_set_style_text_font(center_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(center_text, COLOR_TEXT, 0);
    lv_obj_set_style_text_align(center_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(center_text, AVATAR_SIZE - 20);
    lv_obj_center(center_text);
    lv_obj_add_flag(center_text, LV_OBJ_FLAG_HIDDEN);
    
    // Adjustment arc (hidden by default, shown in adjustment mode)
    adjust_arc = lv_arc_create(center_obj);
    lv_obj_set_size(adjust_arc, AVATAR_SIZE - 10, AVATAR_SIZE - 10);
    lv_obj_center(adjust_arc);
    lv_arc_set_rotation(adjust_arc, 135);  // Start from bottom-left
    lv_arc_set_bg_angles(adjust_arc, 0, 270);  // 270 degree sweep
    lv_arc_set_value(adjust_arc, 50);
    lv_obj_set_style_arc_color(adjust_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(adjust_arc, COLOR_SELECTED, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(adjust_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(adjust_arc, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(adjust_arc, NULL, LV_PART_KNOB);  // No knob
    lv_obj_clear_flag(adjust_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(adjust_arc, LV_OBJ_FLAG_HIDDEN);
    
    ui_initialized = true;
    update_highlight();
    
    ESP_LOGI(TAG, "Wedge UI initialized");
    return true;
}

void wedge_ui_set_avatar_state(avatar_state_t state) {
    if (!ui_initialized || !avatar_img) return;
    if (state == current_avatar_state) return;
    
    current_avatar_state = state;
    
    const lv_img_dsc_t* img = &avatar_idle_dsc;
    switch (state) {
        case AVATAR_LISTENING:
            img = &avatar_listening_dsc;
            break;
        case AVATAR_THINKING:
            img = &avatar_thinking_dsc;
            break;
        case AVATAR_SPEAKING:
            img = &avatar_speaking_dsc;
            break;
        case AVATAR_NOTIFICATION:
            img = &avatar_notification_dsc;
            break;
        default:
            img = &avatar_idle_dsc;
            break;
    }
    
    lv_img_set_src(avatar_img, img);
}

void wedge_ui_set_selection(int wedge) {
    if (wedge < 0 || wedge > 7) return;
    if (wedge == selected_wedge) return;
    
    selected_wedge = wedge;
    if (ui_initialized) {
        // LVGL operations must be protected - may be called from encoder_task!
        if (display_lock(50)) {
            update_highlight();  // This now also updates center content
            display_unlock();
        }
    }
}

int wedge_ui_get_selection(void) {
    return selected_wedge;
}

void wedge_ui_rotate(int delta) {
    int new_wedge = (selected_wedge + delta + 8) % 8;
    wedge_ui_set_selection(new_wedge);
}

// Update all wedge labels for current menu
static void update_all_labels(void) {
    for (int i = 0; i < 8; i++) {
        if (wedge_labels_obj[i]) {
            lv_label_set_text(wedge_labels_obj[i], current_labels[i]);
        }
    }
    update_highlight();
}

// Track if we're in Music/Zoom mode
static bool music_mode_active = false;

// Switch to a different menu
static void switch_menu(menu_id_t menu) {
    menu_id_t old_menu = current_menu;
    current_menu = menu;
    
    // Handle resource switching for Music/Zoom modes
    bool entering_music_mode = (menu == MENU_MUSIC);
    bool leaving_music_mode = (old_menu == MENU_MUSIC) && (menu == MENU_MAIN);
    
    // Command server now runs at boot (started in main.c after WiFi connects)
    // Just track mode for UI display purposes
    if (entering_music_mode) {
        ESP_LOGI(TAG, "Entering Music mode");
        music_mode_active = true;
    } else if (leaving_music_mode) {
        ESP_LOGI(TAG, "Leaving Music mode");
        music_mode_active = false;
    }
    
    switch (menu) {
        case MENU_SETTINGS:
            current_labels = settings_menu_labels;
            break;
        case MENU_MUSIC:
            current_labels = music_menu_labels;
            break;
        case MENU_MAIN:
        default:
            current_labels = main_menu_labels;
            break;
    }
    
    selected_wedge = 0;
    update_all_labels();
    update_center_content();  // Update center for new menu
    
    ESP_LOGI(TAG, "Switched to menu: %d", menu);
}

// Handle center tap - returns action to perform
wedge_action_t wedge_ui_center_tap(void) {
    ESP_LOGI(TAG, "Center tap: menu=%d, wedge=%d, adjusting=%d", current_menu, selected_wedge, adjust_mode);
    
    // If in adjustment mode, center tap exits and saves
    if (adjust_mode != ADJUST_NONE) {
        ESP_LOGI(TAG, "Exiting adjustment mode, final value=%d", adjust_value);
        exit_adjustment_mode();
        return ACTION_NONE;
    }
    
    if (current_menu == MENU_MAIN) {
        switch (selected_wedge) {
            case 0:  // Minerva - start voice
                return ACTION_VOICE_START;
            case 1:  // Music - open music control
                switch_menu(MENU_MUSIC);
                return ACTION_SUBMENU;
            case 7:  // Settings - open submenu
                switch_menu(MENU_SETTINGS);
                return ACTION_SUBMENU;
            default:
                // Other wedges - no action yet
                return ACTION_NONE;
        }
    } else if (current_menu == MENU_MUSIC) {
        switch (selected_wedge) {
            case 0:  // Back
                switch_menu(MENU_MAIN);
                return ACTION_BACK;
            case 1:  // Previous track
                ESP_LOGI(TAG, "Music: Previous track");
                command_send_prev_track();
                return ACTION_NONE;
            case 2:  // Next track
                ESP_LOGI(TAG, "Music: Next track");
                command_send_next_track();
                return ACTION_NONE;
            case 3:  // Play/Pause
                ESP_LOGI(TAG, "Music: Play/Pause");
                command_send_play_pause();
                return ACTION_NONE;
            case 4:  // Volume down
                ESP_LOGI(TAG, "Music: Volume down");
                command_send_volume_down();
                return ACTION_NONE;
            case 5:  // Volume up
                ESP_LOGI(TAG, "Music: Volume up");
                command_send_volume_up();
                return ACTION_NONE;
            case 6:  // Mute
                ESP_LOGI(TAG, "Music: Mute");
                command_send_mute();
                return ACTION_NONE;
            case 7:  // Spotify
                ESP_LOGI(TAG, "Music: Launch Spotify");
                command_send("launch:spotify", NULL);
                return ACTION_NONE;
            default:
                return ACTION_NONE;
        }
    } else if (current_menu == MENU_SETTINGS) {
        switch (selected_wedge) {
            case 0:  // Back
                switch_menu(MENU_MAIN);
                return ACTION_BACK;
            case 1:  // OTA - Check/Install updates
                if (ota_state == OTA_STATE_UPDATE_AVAILABLE) {
                    // Install the update
                    ota_state = OTA_STATE_INSTALLING;
                    update_center_content();
                    ESP_LOGI(TAG, "Installing OTA update...");
                    return ACTION_OTA_INSTALL;
                } else if (ota_state == OTA_STATE_IDLE || ota_state == OTA_STATE_NO_UPDATE) {
                    // Check for updates
                    ota_state = OTA_STATE_CHECKING;
                    update_center_content();
                    ESP_LOGI(TAG, "Checking for updates...");
                    return ACTION_OTA_CHECK;
                }
                return ACTION_NONE;
            case 2:  // Toggle Wake Word
                wakeword_enabled = !wakeword_enabled;
                ESP_LOGI(TAG, "Wake word: %s", wakeword_enabled ? "enabled" : "disabled");
                update_center_content();  // Refresh to show new state
                return ACTION_TOGGLE_WAKEWORD;
            case 3:  // Brightness
                enter_adjustment_mode(ADJUST_BRIGHTNESS, (display_get_brightness() * 100) / 255);
                return ACTION_NONE;
            case 4:  // Volume
                enter_adjustment_mode(ADJUST_VOLUME, 50);
                return ACTION_NONE;
            case 6:  // (was Bluetooth - now unused)
                return ACTION_NONE;
            case 7:  // Restart
                ESP_LOGI(TAG, "Restart requested");
                esp_restart();
                return ACTION_NONE;
            default:
                return ACTION_NONE;
        }
    }
    
    return ACTION_NONE;
}

menu_id_t wedge_ui_get_menu(void) {
    return current_menu;
}

bool wedge_ui_is_ota_mode(void) {
    return ota_mode;
}

void wedge_ui_exit_ota_mode(void) {
    ota_mode = false;
    ESP_LOGI(TAG, "OTA Mode disabled");
}

void wedge_ui_set_update_available(bool available) {
    ota_state = available ? OTA_STATE_UPDATE_AVAILABLE : OTA_STATE_NO_UPDATE;
    if (display_lock(50)) {
        update_center_content();
        display_unlock();
    }
    ESP_LOGI(TAG, "Update available: %s", available ? "yes" : "no");
}

void wedge_ui_reset_ota_state(void) {
    ota_state = OTA_STATE_IDLE;
    if (display_lock(50)) {
        update_center_content();
        display_unlock();
    }
}

bool wedge_ui_is_adjusting(void) {
    return adjust_mode != ADJUST_NONE;
}

static void enter_adjustment_mode(adjust_mode_t mode, int initial_value) {
    adjust_mode = mode;
    adjust_value = initial_value;
    
    if (adjust_arc) {
        lv_arc_set_value(adjust_arc, adjust_value);
        lv_obj_clear_flag(adjust_arc, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update center text to show value
    if (center_text) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", adjust_value);
        lv_label_set_text(center_text, buf);
    }
    
    ESP_LOGI(TAG, "Entered adjustment mode %d, value=%d", mode, adjust_value);
}

static void exit_adjustment_mode(void) {
    adjust_mode = ADJUST_NONE;
    
    if (adjust_arc) {
        lv_obj_add_flag(adjust_arc, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Restore center content
    update_center_content();
    
    ESP_LOGI(TAG, "Exited adjustment mode");
}

void wedge_ui_adjust_value(int delta) {
    if (adjust_mode == ADJUST_NONE) return;
    
    adjust_value += delta;
    if (adjust_value < 0) adjust_value = 0;
    if (adjust_value > 100) adjust_value = 100;
    
    // LVGL operations must be protected - called from encoder_task!
    if (display_lock(50)) {
        if (adjust_arc) {
            lv_arc_set_value(adjust_arc, adjust_value);
        }
        
        if (center_text) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d%%", adjust_value);
            lv_label_set_text(center_text, buf);
        }
        display_unlock();
    }
    
    // Apply the value in real-time (no LVGL lock needed - just LEDC)
    if (adjust_mode == ADJUST_BRIGHTNESS) {
        display_set_brightness((adjust_value * 255) / 100);
    }
    
    ESP_LOGI(TAG, "Adjust value: %d%%", adjust_value);
}
