/**
 * WiFi Manager with Captive Portal
 * - First boot: AP mode with captive portal for WiFi setup
 * - Subsequent boots: Connect to saved WiFi
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include "esp_http_server.h"

// WiFi states
enum WiFiState {
    WIFI_STATE_IDLE,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED
};

// Initialize WiFi manager
void wifi_manager_init();

// Main loop handler
void wifi_manager_loop();

// Get current state
WiFiState wifi_manager_get_state();

// Get status string for display
const char* wifi_manager_get_status();

// Get IP address string
String wifi_manager_get_ip();

// Force AP mode (for settings reset)
void wifi_manager_start_ap();

// Check if credentials are saved
bool wifi_manager_has_credentials();

// Clear saved credentials
void wifi_manager_clear_credentials();
httpd_handle_t wifi_manager_get_server();

#endif // WIFI_MANAGER_H
