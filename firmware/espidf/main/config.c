/**
 * Configuration Manager Implementation
 */

#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "config";

// Current config (in RAM)
static config_t config = {0};

// NVS namespace
#define NVS_NAMESPACE "voicehub"

// Load a string from NVS
static void load_string(nvs_handle_t nvs, const char* key, char* dest, size_t max_len)
{
    size_t len = max_len;
    if (nvs_get_str(nvs, key, dest, &len) != ESP_OK) {
        dest[0] = '\0';
    }
}

// Save a string to NVS
static void save_string(const char* key, const char* value)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, key, value);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// Save a uint8 to NVS
static void save_u8(const char* key, uint8_t value)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, key, value);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

void config_init(void)
{
    ESP_LOGI(TAG, "Loading configuration from NVS");
    
    // Set defaults
    memset(&config, 0, sizeof(config));
    config.brightness = 255;
    config.wakeword_enabled = true;
    config.volume = 80;
    
    // Open NVS
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "No saved config in voicehub namespace");
        
        // Try migrating from old "wifi" namespace
        nvs_handle_t old_nvs;
        if (nvs_open("wifi", NVS_READONLY, &old_nvs) == ESP_OK) {
            ESP_LOGI(TAG, "Migrating from old wifi namespace");
            load_string(old_nvs, "ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
            load_string(old_nvs, "password", config.wifi_password, sizeof(config.wifi_password));
            nvs_close(old_nvs);
            
            // Save to new namespace
            if (config.wifi_ssid[0] != '\0') {
                config_set_wifi(config.wifi_ssid, config.wifi_password);
                ESP_LOGI(TAG, "Migrated WiFi credentials: %s", config.wifi_ssid);
            }
        }
        
        // Fallback: hardcoded credentials for development (remove for production)
        if (config.wifi_ssid[0] == '\0') {
            ESP_LOGW(TAG, "Using hardcoded fallback WiFi credentials");
            strcpy(config.wifi_ssid, "Hyperoptic Fibre 6A50 - 2GH");
            strcpy(config.wifi_password, "Ns4R97HZ3ACbts");
        }
        
        // Fallback: hardcoded OpenClaw for development
        if (config.openclaw_url[0] == '\0') {
            strcpy(config.openclaw_url, "ws://192.168.1.223:8765");
        }
        return;
    }
    
    // Load WiFi
    load_string(nvs, "wifi_ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
    load_string(nvs, "wifi_pass", config.wifi_password, sizeof(config.wifi_password));
    
    // Load OpenClaw
    load_string(nvs, "oc_url", config.openclaw_url, sizeof(config.openclaw_url));
    load_string(nvs, "oc_token", config.openclaw_token, sizeof(config.openclaw_token));
    
    // Load device settings
    uint8_t val;
    if (nvs_get_u8(nvs, "brightness", &val) == ESP_OK) {
        config.brightness = val;
    }
    if (nvs_get_u8(nvs, "wakeword", &val) == ESP_OK) {
        config.wakeword_enabled = (val != 0);
    }
    if (nvs_get_u8(nvs, "volume", &val) == ESP_OK) {
        config.volume = val;
    }
    
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Config loaded - WiFi: %s, OpenClaw: %s", 
             config.wifi_ssid[0] ? config.wifi_ssid : "(none)",
             config.openclaw_url[0] ? config.openclaw_url : "(none)");
}

const config_t* config_get(void)
{
    return &config;
}

void config_set_wifi(const char* ssid, const char* password)
{
    strncpy(config.wifi_ssid, ssid ? ssid : "", CONFIG_MAX_SSID_LEN);
    strncpy(config.wifi_password, password ? password : "", CONFIG_MAX_PASSWORD_LEN);
    config.wifi_ssid[CONFIG_MAX_SSID_LEN] = '\0';
    config.wifi_password[CONFIG_MAX_PASSWORD_LEN] = '\0';
    
    save_string("wifi_ssid", config.wifi_ssid);
    save_string("wifi_pass", config.wifi_password);
    
    ESP_LOGI(TAG, "WiFi credentials saved: %s", config.wifi_ssid);
}

void config_set_openclaw(const char* url, const char* token)
{
    strncpy(config.openclaw_url, url ? url : "", CONFIG_MAX_URL_LEN);
    strncpy(config.openclaw_token, token ? token : "", CONFIG_MAX_TOKEN_LEN);
    config.openclaw_url[CONFIG_MAX_URL_LEN] = '\0';
    config.openclaw_token[CONFIG_MAX_TOKEN_LEN] = '\0';
    
    save_string("oc_url", config.openclaw_url);
    save_string("oc_token", config.openclaw_token);
    
    ESP_LOGI(TAG, "OpenClaw config saved: %s", config.openclaw_url);
}

void config_set_brightness(uint8_t brightness)
{
    config.brightness = brightness;
    save_u8("brightness", brightness);
}

void config_set_wakeword(bool enabled)
{
    config.wakeword_enabled = enabled;
    save_u8("wakeword", enabled ? 1 : 0);
}

void config_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    config.volume = volume;
    save_u8("volume", volume);
}

bool config_has_wifi(void)
{
    return config.wifi_ssid[0] != '\0' && config.wifi_password[0] != '\0';
}

bool config_has_openclaw(void)
{
    return config.openclaw_url[0] != '\0';
}

void config_reset(void)
{
    ESP_LOGW(TAG, "Resetting all configuration");
    
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    
    // Re-init with defaults
    config_init();
}
