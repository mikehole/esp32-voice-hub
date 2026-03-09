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
#include "display.h"
#include "audio.h"

static const char *TAG = "voice_hub";

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

    // Initialize display (LVGL)
    display_init();
    ESP_LOGI(TAG, "Display initialized");

    // Initialize audio (I2S mic + DAC)
    audio_init();
    ESP_LOGI(TAG, "Audio initialized");

    // Initialize WiFi
    wifi_manager_init();
    ESP_LOGI(TAG, "WiFi manager initialized");

    // Start WiFi connection (non-blocking)
    wifi_manager_start();

    // Main loop - handle LVGL and system tasks
    while (1) {
        display_loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
