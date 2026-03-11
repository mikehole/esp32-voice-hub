/**
 * Configuration Manager
 * Stores settings in NVS (WiFi, OpenClaw endpoint, etc.)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum lengths for config strings
#define CONFIG_MAX_SSID_LEN      32
#define CONFIG_MAX_PASSWORD_LEN  64
#define CONFIG_MAX_URL_LEN       128
#define CONFIG_MAX_TOKEN_LEN     128

// Configuration structure
typedef struct {
    // WiFi
    char wifi_ssid[CONFIG_MAX_SSID_LEN + 1];
    char wifi_password[CONFIG_MAX_PASSWORD_LEN + 1];
    
    // OpenClaw
    char openclaw_url[CONFIG_MAX_URL_LEN + 1];      // WebSocket URL, e.g., ws://192.168.1.100:8765
    char openclaw_token[CONFIG_MAX_TOKEN_LEN + 1];  // Optional auth token
    
    // Device
    uint8_t brightness;      // 0-255
    bool wakeword_enabled;   // Wake word detection on/off
    uint8_t volume;          // 0-100
} config_t;

// Initialize config system (loads from NVS)
void config_init(void);

// Get current config (read-only pointer)
const config_t* config_get(void);

// Setters (automatically persist to NVS)
void config_set_wifi(const char* ssid, const char* password);
void config_set_openclaw(const char* url, const char* token);
void config_set_brightness(uint8_t brightness);
void config_set_wakeword(bool enabled);
void config_set_volume(uint8_t volume);

// Check if WiFi is configured
bool config_has_wifi(void);

// Check if OpenClaw is configured
bool config_has_openclaw(void);

// Reset all config to defaults
void config_reset(void);

#ifdef __cplusplus
}
#endif
