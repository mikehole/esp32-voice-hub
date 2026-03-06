/**
 * WiFi Manager with Captive Portal
 * Using ESP-IDF WiFi directly for pioarduino compatibility
 */

#include "wifi_manager.h"
#include <Arduino.h>
#include <Preferences.h>

// ESP-IDF includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/ip6.h"
#include "lwip/netif.h"

// IPv6 hook required by ESP-IDF lwip build
extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp) {
    // Return 0 to continue processing, 1 to drop
    return 0;
}

// Simple HTTP server instead of AsyncWebServer
#include "esp_http_server.h"
#include "web_admin.h"

// AP Configuration
#define AP_SSID "Minerva-Setup"
#define AP_PASS ""
#define AP_CHANNEL 1
#define AP_MAX_CONN 4

// Preferences namespace
#define PREF_NAMESPACE "wifi"
#define PREF_SSID "ssid"
#define PREF_PASS "pass"

// Connection timeout and retries
#define CONNECT_TIMEOUT_MS 15000
#define MAX_CONNECT_RETRIES 3

// Global state
static WiFiState current_state = WIFI_STATE_IDLE;
static String status_message = "Initializing...";
static Preferences preferences;
static httpd_handle_t server_handle = NULL;
static esp_netif_t* ap_netif = NULL;
static esp_netif_t* sta_netif = NULL;
static unsigned long connect_start_time = 0;
static char current_ip[16] = "0.0.0.0";
static bool wifi_connected = false;
static int connect_retry_count = 0;

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void start_webserver();

// HTML in separate header for cleaner escaping
#include "captive_portal.h"

// Event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                Serial.println("WiFi: STA started");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                Serial.println("WiFi: Disconnected");
                wifi_connected = false;
                if (current_state == WIFI_STATE_CONNECTING) {
                    // Will be handled by timeout
                }
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                Serial.println("WiFi: Client connected to AP");
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&event->ip_info.ip));
            Serial.printf("WiFi: Got IP: %s\n", current_ip);
            
            // Set router as primary DNS (for .mynet), Google as fallback
            ip_addr_t dns1, dns2;
            IP_ADDR4(&dns1, 192, 168, 1, 1);  // Router DNS (resolves .mynet)
            IP_ADDR4(&dns2, 8, 8, 8, 8);      // Google DNS fallback
            dns_setserver(0, &dns1);
            dns_setserver(1, &dns2);
            Serial.println("WiFi: DNS set to 192.168.1.1, 8.8.8.8");
            
            wifi_connected = true;
            // Don't set WIFI_STATE_CONNECTED here - let loop() handle it so webserver starts
            status_message = String("Connected: ") + current_ip;
        }
    }
}

// HTTP handlers
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CAPTIVE_PORTAL_HTML, strlen(CAPTIVE_PORTAL_HTML));
    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = { 0 };
    esp_wifi_scan_start(&scan_config, true);
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(ap_count * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    
    String json = "[";
    for (int i = 0; i < ap_count && i < 20; i++) {
        if (i > 0) json += ",";
        json += "{\"s\":\"" + String((char*)ap_records[i].ssid) + "\",\"r\":" + String(ap_records[i].rssi) + "}";
    }
    json += "]";
    free(ap_records);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

// URL decode helper
static String url_decode(const char* str) {
    String result;
    char hex[3] = {0};
    while (*str) {
        if (*str == '%' && str[1] && str[2]) {
            hex[0] = str[1];
            hex[1] = str[2];
            result += (char)strtol(hex, NULL, 16);
            str += 3;
        } else if (*str == '+') {
            result += ' ';
            str++;
        } else {
            result += *str++;
        }
    }
    return result;
}

static esp_err_t connect_handler(httpd_req_t *req) {
    char query[256] = {0};
    char ssid[64] = {0};
    char pass[64] = {0};
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "ssid", ssid, sizeof(ssid));
        httpd_query_key_value(query, "pass", pass, sizeof(pass));
    }
    
    // URL decode
    String ssid_str = url_decode(ssid);
    String pass_str = url_decode(pass);
    
    if (ssid_str.length() == 0) {
        httpd_resp_send(req, "Error: SSID required", -1);
        return ESP_OK;
    }
    
    Serial.printf("WiFi: Connecting to: %s (pass len: %d)\n", ssid_str.c_str(), pass_str.length());
    
    // Save credentials
    preferences.putString(PREF_SSID, ssid_str);
    preferences.putString(PREF_PASS, pass_str);
    
    // Configure station
    wifi_config_t sta_config = { 0 };
    strncpy((char*)sta_config.sta.ssid, ssid_str.c_str(), sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, pass_str.c_str(), sizeof(sta_config.sta.password) - 1);
    
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();
    
    // Wait for connection
    int attempts = 0;
    while (!wifi_connected && attempts < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }
    
    if (wifi_connected) {
        String response = "OK IP: " + String(current_ip);
        httpd_resp_send(req, response.c_str(), response.length());
        
        // Restart after short delay
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        preferences.remove(PREF_SSID);
        preferences.remove(PREF_PASS);
        httpd_resp_send(req, "Failed to connect", -1);
    }
    
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 32;  // Increased for admin + audio + TTS + Minerva control endpoints
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;      // Larger stack for handlers
    config.max_open_sockets = 7;   // More concurrent connections
    config.lru_purge_enable = true; // Auto-close idle connections
    config.recv_wait_timeout = 5;  // 5 second timeout
    config.send_wait_timeout = 5;
    
    if (httpd_start(&server_handle, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
        httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_handler };
        httpd_uri_t conn = { .uri = "/connect", .method = HTTP_GET, .handler = connect_handler };
        httpd_uri_t redir = { .uri = "/*", .method = HTTP_GET, .handler = redirect_handler };
        
        httpd_register_uri_handler(server_handle, &root);
        httpd_register_uri_handler(server_handle, &scan);
        httpd_register_uri_handler(server_handle, &conn);
        
        // Register admin endpoints
        web_admin_register(server_handle);
        
        // Wildcard redirect must be last
        httpd_register_uri_handler(server_handle, &redir);
        
        Serial.println("WiFi: Web server started");
    }
}

void wifi_manager_init() {
    preferences.begin(PREF_NAMESPACE, false);
    
    // Initialize TCP/IP and event loop
    esp_netif_init();
    esp_event_loop_create_default();
    
    // Register event handlers
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    
    // Create default netifs
    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();
    
    // Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    
    String saved_ssid = preferences.getString(PREF_SSID, "");
    String saved_pass = preferences.getString(PREF_PASS, "");
    
    if (saved_ssid.length() > 0) {
        Serial.printf("WiFi: Connecting to saved: %s\n", saved_ssid.c_str());
        status_message = "Connecting to " + saved_ssid + "...";
        current_state = WIFI_STATE_CONNECTING;
        
        // Configure as station
        esp_wifi_set_mode(WIFI_MODE_STA);
        
        wifi_config_t sta_config = { 0 };
        strncpy((char*)sta_config.sta.ssid, saved_ssid.c_str(), sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, saved_pass.c_str(), sizeof(sta_config.sta.password) - 1);
        
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        esp_wifi_start();
        esp_wifi_connect();
        
        connect_start_time = millis();
    } else {
        Serial.println("WiFi: No credentials, starting AP");
        wifi_manager_start_ap();
    }
}

void wifi_manager_loop() {
    static bool webserver_started = false;
    
    if (current_state == WIFI_STATE_CONNECTING) {
        if (wifi_connected) {
            current_state = WIFI_STATE_CONNECTED;
            connect_retry_count = 0;
            Serial.println("WiFi: Connected, starting webserver");
            start_webserver();
            webserver_started = true;
        } else if (millis() - connect_start_time > CONNECT_TIMEOUT_MS) {
            connect_retry_count++;
            Serial.printf("WiFi: Timeout (attempt %d/%d)\n", connect_retry_count, MAX_CONNECT_RETRIES);
            
            if (connect_retry_count < MAX_CONNECT_RETRIES) {
                // Retry connection
                Serial.println("WiFi: Retrying...");
                esp_wifi_disconnect();
                delay(500);
                esp_wifi_connect();
                connect_start_time = millis();
            } else {
                // Give up, start AP
                Serial.println("WiFi: Max retries reached, starting AP");
                current_state = WIFI_STATE_FAILED;
                connect_retry_count = 0;
                wifi_manager_start_ap();
                webserver_started = true;
            }
        }
    }
    
    // Catch case where connection happened before loop started checking
    if (current_state == WIFI_STATE_CONNECTED && !webserver_started && server_handle == NULL) {
        Serial.println("WiFi: Late webserver start");
        start_webserver();
        webserver_started = true;
    }
}

WiFiState wifi_manager_get_state() {
    return current_state;
}

const char* wifi_manager_get_status() {
    return status_message.c_str();
}

String wifi_manager_get_ip() {
    return String(current_ip);
}

void wifi_manager_start_ap() {
    Serial.println("WiFi: Starting AP mode");
    
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    
    wifi_config_t ap_config = { 0 };
    strncpy((char*)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(AP_SSID);
    ap_config.ap.channel = AP_CHANNEL;
    ap_config.ap.max_connection = AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    
    // Get AP IP
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&ip_info.ip));
    
    current_state = WIFI_STATE_AP_MODE;
    status_message = String("AP: ") + AP_SSID;
    
    Serial.printf("WiFi: AP started - %s @ %s\n", AP_SSID, current_ip);
    
    start_webserver();
}

bool wifi_manager_has_credentials() {
    return preferences.getString(PREF_SSID, "").length() > 0;
}

void wifi_manager_clear_credentials() {
    preferences.remove(PREF_SSID);
    preferences.remove(PREF_PASS);
    Serial.println("WiFi: Credentials cleared");
}

httpd_handle_t wifi_manager_get_server() {
    return server_handle;
}
