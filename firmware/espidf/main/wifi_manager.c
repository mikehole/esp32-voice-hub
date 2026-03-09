/**
 * WiFi Manager - ESP-IDF native implementation
 * Handles connection, reconnection, and state management
 */

#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_mgr";

// Event group for WiFi events
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// State
static wifi_state_t current_state = WIFI_STATE_IDLE;
static char ip_address[16] = {0};
static wifi_state_callback_t state_callback = NULL;
static int retry_count = 0;
#define MAX_RETRY 5

static void set_state(wifi_state_t new_state)
{
    if (current_state != new_state) {
        current_state = new_state;
        ESP_LOGI(TAG, "State changed: %d", new_state);
        if (state_callback) {
            state_callback(new_state);
        }
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                set_state(WIFI_STATE_CONNECTING);
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                if (retry_count < MAX_RETRY) {
                    ESP_LOGI(TAG, "Disconnected, retrying... (%d/%d)", retry_count + 1, MAX_RETRY);
                    set_state(WIFI_STATE_CONNECTING);
                    esp_wifi_connect();
                    retry_count++;
                } else {
                    ESP_LOGE(TAG, "Max retries reached");
                    set_state(WIFI_STATE_ERROR);
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", ip_address);
        retry_count = 0;
        set_state(WIFI_STATE_CONNECTED);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

void wifi_manager_start(void)
{
    // Load stored credentials from NVS
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    
    wifi_config_t wifi_config = {0};
    bool have_creds = false;
    
    if (err == ESP_OK) {
        size_t ssid_len = sizeof(wifi_config.sta.ssid);
        size_t pass_len = sizeof(wifi_config.sta.password);
        
        if (nvs_get_str(nvs, "ssid", (char*)wifi_config.sta.ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs, "password", (char*)wifi_config.sta.password, &pass_len) == ESP_OK) {
            have_creds = true;
            ESP_LOGI(TAG, "Using saved credentials for: %s", wifi_config.sta.ssid);
        }
        nvs_close(nvs);
    }
    
    // Fallback to hardcoded credentials if NVS is empty
    if (!have_creds) {
        ESP_LOGI(TAG, "Using default credentials");
        strcpy((char*)wifi_config.sta.ssid, "Hyperoptic Fibre 6A50");
        strcpy((char*)wifi_config.sta.password, "Ns4R97HZ3ACbts");
        have_creds = true;
    }
    
    if (have_creds) {
        ESP_LOGI(TAG, "Connecting to: %s", wifi_config.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "No credentials available");
        set_state(WIFI_STATE_ERROR);
    }
}

wifi_state_t wifi_manager_get_state(void)
{
    return current_state;
}

const char* wifi_manager_get_ip(void)
{
    return ip_address;
}

esp_err_t wifi_manager_set_credentials(const char* ssid, const char* password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(nvs, "ssid", ssid);
    if (err != ESP_OK) { nvs_close(nvs); return err; }
    
    err = nvs_set_str(nvs, "password", password);
    if (err != ESP_OK) { nvs_close(nvs); return err; }
    
    err = nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Credentials saved for: %s", ssid);
    return err;
}

void wifi_manager_set_callback(wifi_state_callback_t cb)
{
    state_callback = cb;
}
