#pragma once

#include "esp_err.h"

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

// Initialize WiFi subsystem
void wifi_manager_init(void);

// Start WiFi connection (uses stored credentials or starts AP mode)
void wifi_manager_start(void);

// Get current WiFi state
wifi_state_t wifi_manager_get_state(void);

// Get IP address string (valid when connected)
const char* wifi_manager_get_ip(void);

// Set WiFi credentials and connect
esp_err_t wifi_manager_set_credentials(const char* ssid, const char* password);

// Callback registration for state changes
typedef void (*wifi_state_callback_t)(wifi_state_t state);
void wifi_manager_set_callback(wifi_state_callback_t cb);
