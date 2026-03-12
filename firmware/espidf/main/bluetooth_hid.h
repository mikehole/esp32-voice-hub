/**
 * @file bluetooth_hid.h
 * @brief Bluetooth HID (keyboard + media control) interface
 * 
 * Implements BLE HID device for keyboard shortcuts and media keys.
 * Used by Music wedge (play/pause, volume) and Zoom wedge (mute, camera).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth connection state
 */
typedef enum {
    BT_STATE_OFF,           // Bluetooth disabled
    BT_STATE_ADVERTISING,   // Discoverable, waiting for connection
    BT_STATE_CONNECTED,     // Connected to host
    BT_STATE_PAIRING,       // Pairing in progress
} bt_state_t;

/**
 * @brief HID Consumer Control keys (media keys)
 */
typedef enum {
    HID_CC_PLAY_PAUSE   = 0xCD,
    HID_CC_NEXT_TRACK   = 0xB5,
    HID_CC_PREV_TRACK   = 0xB6,
    HID_CC_VOLUME_UP    = 0xE9,
    HID_CC_VOLUME_DOWN  = 0xEA,
    HID_CC_MUTE         = 0xE2,
} hid_consumer_key_t;

/**
 * @brief HID Keyboard modifier keys
 */
typedef enum {
    HID_MOD_NONE    = 0x00,
    HID_MOD_LCTRL   = 0x01,
    HID_MOD_LSHIFT  = 0x02,
    HID_MOD_LALT    = 0x04,
    HID_MOD_LGUI    = 0x08,  // Windows/Cmd key
    HID_MOD_RCTRL   = 0x10,
    HID_MOD_RSHIFT  = 0x20,
    HID_MOD_RALT    = 0x40,
    HID_MOD_RGUI    = 0x80,
} hid_modifier_t;

/**
 * @brief Common HID keyboard scancodes
 */
typedef enum {
    HID_KEY_A = 0x04,
    HID_KEY_B = 0x05,
    HID_KEY_C = 0x06,
    HID_KEY_D = 0x07,
    HID_KEY_E = 0x08,
    HID_KEY_F = 0x09,
    HID_KEY_G = 0x0A,
    HID_KEY_H = 0x0B,
    HID_KEY_I = 0x0C,
    HID_KEY_J = 0x0D,
    HID_KEY_K = 0x0E,
    HID_KEY_L = 0x0F,
    HID_KEY_M = 0x10,
    HID_KEY_N = 0x11,
    HID_KEY_O = 0x12,
    HID_KEY_P = 0x13,
    HID_KEY_Q = 0x14,
    HID_KEY_R = 0x15,
    HID_KEY_S = 0x16,
    HID_KEY_T = 0x17,
    HID_KEY_U = 0x18,
    HID_KEY_V = 0x19,
    HID_KEY_W = 0x1A,
    HID_KEY_X = 0x1B,
    HID_KEY_Y = 0x1C,
    HID_KEY_Z = 0x1D,
    HID_KEY_SPACE = 0x2C,
    HID_KEY_ENTER = 0x28,
    HID_KEY_ESC = 0x29,
} hid_key_t;

/**
 * @brief Initialize Bluetooth HID subsystem
 * @return true on success
 */
bool bluetooth_hid_init(void);

/**
 * @brief Deinitialize Bluetooth HID subsystem
 */
void bluetooth_hid_deinit(void);

/**
 * @brief Start advertising (make device discoverable)
 * @return true on success
 */
bool bluetooth_hid_start_advertising(void);

/**
 * @brief Stop advertising
 */
void bluetooth_hid_stop_advertising(void);

/**
 * @brief Disconnect current connection
 */
void bluetooth_hid_disconnect(void);

/**
 * @brief Get current Bluetooth state
 */
bt_state_t bluetooth_hid_get_state(void);

/**
 * @brief Check if connected to a host
 */
bool bluetooth_hid_is_connected(void);

/**
 * @brief Get connected device name (if available)
 * @param buf Buffer to store name
 * @param len Buffer length
 * @return true if name available
 */
bool bluetooth_hid_get_peer_name(char* buf, size_t len);

/**
 * @brief Send a consumer control (media) key
 * @param key Consumer control key code
 * @return true on success
 */
bool bluetooth_hid_send_consumer_key(hid_consumer_key_t key);

/**
 * @brief Send a keyboard key with modifiers
 * @param modifiers Modifier key bitmask (HID_MOD_*)
 * @param key HID scancode (HID_KEY_*)
 * @return true on success
 */
bool bluetooth_hid_send_key(uint8_t modifiers, uint8_t key);

/**
 * @brief Send keyboard shortcut (e.g., Alt+A for Zoom mute)
 * @param modifiers Modifier key bitmask
 * @param key HID scancode
 * @return true on success
 * 
 * This sends key-down, waits briefly, then key-up.
 */
bool bluetooth_hid_send_shortcut(uint8_t modifiers, uint8_t key);

/**
 * @brief Convenience: Send volume up
 */
bool bluetooth_hid_volume_up(void);

/**
 * @brief Convenience: Send volume down
 */
bool bluetooth_hid_volume_down(void);

/**
 * @brief Convenience: Send play/pause
 */
bool bluetooth_hid_play_pause(void);

/**
 * @brief Convenience: Send next track
 */
bool bluetooth_hid_next_track(void);

/**
 * @brief Convenience: Send previous track
 */
bool bluetooth_hid_prev_track(void);

/**
 * @brief Convenience: Send mute
 */
bool bluetooth_hid_mute(void);

#ifdef __cplusplus
}
#endif
