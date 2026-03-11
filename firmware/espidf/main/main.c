/**
 * ESP32 Voice Hub - ESP-IDF Version
 * 
 * Clean rewrite with:
 * - Native ESP-SR wake word detection
 * - OTA firmware updates
 * - Stable I2S audio
 * - LVGL display
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "web_server.h"
#include "ota_update.h"
#include "config.h"
#include "display.h"
#include "audio.h"
#include "voice_client.h"
#include "notification.h"

static const char *TAG = "voice_hub";

// Forward declaration
static void on_wifi_state_change(wifi_state_t state);

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32 Voice Hub - Starting...");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS (required for WiFi and settings storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize configuration (loads from NVS)
    config_init();
    ESP_LOGI(TAG, "Configuration loaded");

    // Initialize display (LVGL)
    display_init();
    ESP_LOGI(TAG, "Display initialized");

    // Initialize audio (I2S mic + DAC)
    audio_init();
    ESP_LOGI(TAG, "Audio initialized");

    // Initialize voice client (touch + WebSocket)
    voice_client_init();
    ESP_LOGI(TAG, "Voice client initialized");

    // Initialize notification system
    notification_init();
    ESP_LOGI(TAG, "Notification system initialized");

    // Set up WiFi state callback to start web server when connected
    wifi_manager_set_callback(on_wifi_state_change);
    
    // Initialize WiFi
    wifi_manager_init();
    ESP_LOGI(TAG, "WiFi manager initialized");

    // Start WiFi connection (non-blocking)
    wifi_manager_start();

    // Main loop - handle LVGL and system tasks
    while (1) {
        display_loop();
        
        // Update notification system (plays periodic attention sound)
        notification_update();
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Callback when WiFi state changes
static void on_wifi_state_change(wifi_state_t state)
{
    if (state == WIFI_STATE_AP_MODE) {
        ESP_LOGI(TAG, "AP mode - Captive portal at 192.168.4.1");
        web_server_start();
        ESP_LOGI(TAG, "Setup portal started");
    }
    else if (state == WIFI_STATE_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected! IP: %s", wifi_manager_get_ip());
        ota_init();
        web_server_start();
        ESP_LOGI(TAG, "Web server started - OTA ready!");
        
        // Connect to OpenClaw if configured
        const config_t *cfg = config_get();
        if (config_has_openclaw()) {
            ESP_LOGI(TAG, "Connecting to OpenClaw: %s", cfg->openclaw_url);
            voice_client_connect(cfg->openclaw_url);
        } else {
            ESP_LOGW(TAG, "OpenClaw not configured - use /setup to configure");
        }
        
        // Start wake word detection after a short delay
        vTaskDelay(pdMS_TO_TICKS(1000));
        voice_client_on_connected();
    }
}
