/**
 * WiFi Manager
 * Handles WiFi connection and captive portal for initial setup
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_AP_MODE,        // Running captive portal
    WIFI_STATE_CONNECTING,     // Connecting to saved network
    WIFI_STATE_CONNECTED,      // Connected to WiFi
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

// Initialize WiFi subsystem
void wifi_manager_init(void);

// Start WiFi (connects to saved network, or starts AP if none configured)
void wifi_manager_start(void);

// Force AP mode (for reconfiguration)
void wifi_manager_start_ap(void);

// Connect to a specific network (saves credentials and connects)
esp_err_t wifi_manager_connect(const char* ssid, const char* password);

// Get current WiFi state
wifi_state_t wifi_manager_get_state(void);

// Check if in AP mode (captive portal)
bool wifi_manager_is_ap_mode(void);

// Get IP address string (valid when connected or in AP mode)
const char* wifi_manager_get_ip(void);

// Get AP SSID (when in AP mode)
const char* wifi_manager_get_ap_ssid(void);

// Scan for available networks (returns JSON array string, caller must free)
char* wifi_manager_scan(void);

// Callback registration for state changes
typedef void (*wifi_state_callback_t)(wifi_state_t state);
void wifi_manager_set_callback(wifi_state_callback_t cb);

#ifdef __cplusplus
}
#endif
