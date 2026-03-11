/**
 * WiFi Manager - ESP-IDF native implementation
 * Handles STA connection and AP mode captive portal
 */

#include "wifi_manager.h"
#include "config.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "cJSON.h"

static const char *TAG = "wifi_mgr";

// AP mode settings
#define AP_SSID "ESP32-Voice-Hub"
#define AP_PASS ""  // Open network for easy setup
#define AP_CHANNEL 1
#define AP_MAX_CONN 4

// Event group for WiFi events
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// State
static wifi_state_t current_state = WIFI_STATE_IDLE;
static char ip_address[16] = {0};
static wifi_state_callback_t state_callback = NULL;
static int retry_count = 0;
static bool ap_mode = false;
#define MAX_RETRY 3

// Network interfaces
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

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
                ESP_LOGI(TAG, "STA started, connecting...");
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
                    ESP_LOGW(TAG, "Connection failed, starting AP mode");
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                    // Will switch to AP mode
                    wifi_manager_start_ap();
                }
                break;
                
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started: %s", AP_SSID);
                break;
                
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Client connected to AP");
                break;
                
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Client disconnected from AP");
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Got IP: %s", ip_address);
            retry_count = 0;
            ap_mode = false;
            set_state(WIFI_STATE_CONNECTED);
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        } else if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
            ESP_LOGI(TAG, "Assigned IP to client: " IPSTR, IP2STR(&event->ip));
        }
    }
}

void wifi_manager_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create both STA and AP interfaces
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
}

void wifi_manager_start(void)
{
    const config_t* cfg = config_get();
    
    // Check if we have WiFi credentials
    if (!config_has_wifi()) {
        ESP_LOGI(TAG, "No WiFi credentials, starting AP mode");
        wifi_manager_start_ap();
        return;
    }
    
    // Try to connect to saved network
    ESP_LOGI(TAG, "Connecting to saved network: %s", cfg->wifi_ssid);
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, cfg->wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    retry_count = 0;
    ap_mode = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode: %s", AP_SSID);
    
    // Stop any existing connection
    esp_wifi_stop();
    
    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,  // Open for easy setup
        },
    };
    
    ap_mode = true;
    strcpy(ip_address, "192.168.4.1");
    
    // Use APSTA mode so we can scan for networks while hosting AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    set_state(WIFI_STATE_AP_MODE);
    
    // Do initial scan in background
    ESP_LOGI(TAG, "Starting background scan...");
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to: %s", ssid);
    
    // Save credentials
    config_set_wifi(ssid, password);
    
    // Stop current mode
    esp_wifi_stop();
    
    // Configure and start STA
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    retry_count = 0;
    ap_mode = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    return ESP_OK;
}

wifi_state_t wifi_manager_get_state(void)
{
    return current_state;
}

bool wifi_manager_is_ap_mode(void)
{
    return ap_mode;
}

const char* wifi_manager_get_ip(void)
{
    return ip_address;
}

const char* wifi_manager_get_ap_ssid(void)
{
    return AP_SSID;
}

char* wifi_manager_scan(void)
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // Make sure we're in a mode that supports scanning
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    ESP_LOGI(TAG, "Current WiFi mode: %d (AP=2, STA=1, APSTA=3)", current_mode);
    
    // If pure AP mode, switch to APSTA (should already be APSTA but just in case)
    if (current_mode == WIFI_MODE_AP) {
        ESP_LOGI(TAG, "Switching to APSTA mode for scan");
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(500));  // Give it time to initialize STA
    }
    
    // Start scan with longer timeouts
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,  // Scan all channels
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 120,
        .scan_time.active.max = 500,
    };
    
    ESP_LOGI(TAG, "Calling esp_wifi_scan_start...");
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // Blocking scan
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        return strdup("[]");
    }
    
    // Get results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d networks", ap_count);
    
    if (ap_count == 0) {
        return strdup("[]");
    }
    
    // Limit to 20 networks
    if (ap_count > 20) ap_count = 20;
    
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        return strdup("[]");
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    
    // Build JSON array
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_records[i].rssi);
        cJSON_AddBoolToObject(item, "secure", ap_records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(root, item);
    }
    
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(ap_records);
    
    // Restore AP mode if needed
    if (current_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    }
    
    return json;
}

void wifi_manager_set_callback(wifi_state_callback_t cb)
{
    state_callback = cb;
}
