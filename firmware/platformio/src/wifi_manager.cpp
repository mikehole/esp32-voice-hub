/**
 * WiFi Manager with Captive Portal
 */

#include "wifi_manager.h"

// Use esp_wifi directly for better compatibility with pioarduino
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <string.h>

// Arduino compatibility
#include <Arduino.h>
#include <Preferences.h>

// Async web server
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

// Include WiFi after Arduino
#include <WiFi.h>

// AP Configuration
#define AP_SSID "Minerva-Setup"
#define AP_PASS ""  // Open network for easy setup
#define DNS_PORT 53

// Preferences namespace
#define PREF_NAMESPACE "wifi"
#define PREF_SSID "ssid"
#define PREF_PASS "pass"

// Connection timeout
#define CONNECT_TIMEOUT_MS 15000

// Global state
static WiFiState current_state = WIFI_STATE_IDLE;
static String status_message = "Initializing...";
static AsyncWebServer server(80);
static DNSServer dnsServer;
static Preferences preferences;
static bool server_started = false;
static unsigned long connect_start_time = 0;

// HTML for captive portal
static const char CAPTIVE_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Minerva WiFi Setup</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #0A1929 0%, #0F2744 100%);
            color: #5DADE2;
            margin: 0;
            padding: 20px;
            min-height: 100vh;
            box-sizing: border-box;
        }
        .container {
            max-width: 400px;
            margin: 0 auto;
            background: rgba(15, 39, 68, 0.8);
            border-radius: 20px;
            padding: 30px;
            border: 2px solid #2E86AB;
        }
        h1 {
            text-align: center;
            margin-bottom: 30px;
            font-size: 24px;
        }
        .logo {
            text-align: center;
            font-size: 48px;
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            font-weight: 500;
        }
        input, select {
            width: 100%;
            padding: 12px;
            margin-bottom: 20px;
            border: 2px solid #2E86AB;
            border-radius: 10px;
            background: #0A1929;
            color: #5DADE2;
            font-size: 16px;
            box-sizing: border-box;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #5DADE2;
        }
        button {
            width: 100%;
            padding: 15px;
            background: #5DADE2;
            color: #0A1929;
            border: none;
            border-radius: 10px;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
            transition: background 0.3s;
        }
        button:hover {
            background: #85C1E9;
        }
        .networks {
            margin-bottom: 20px;
        }
        .network-item {
            padding: 12px;
            background: #0A1929;
            border: 1px solid #2E86AB;
            border-radius: 8px;
            margin-bottom: 8px;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .network-item:hover {
            border-color: #5DADE2;
        }
        .signal {
            font-size: 12px;
            opacity: 0.7;
        }
        .status {
            text-align: center;
            padding: 15px;
            border-radius: 10px;
            margin-top: 20px;
        }
        .success { background: #1a4d1a; }
        .error { background: #4d1a1a; }
        .spinner {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 2px solid #5DADE2;
            border-radius: 50%;
            border-top-color: transparent;
            animation: spin 1s linear infinite;
        }
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">🦉</div>
        <h1>Minerva WiFi Setup</h1>
        
        <div id="scan-section">
            <label>Available Networks:</label>
            <div id="networks" class="networks">
                <div style="text-align:center;padding:20px;">
                    <div class="spinner"></div>
                    <p>Scanning...</p>
                </div>
            </div>
        </div>
        
        <form id="wifi-form" action="/connect" method="POST">
            <label for="ssid">Network Name (SSID):</label>
            <input type="text" id="ssid" name="ssid" required placeholder="Enter or select above">
            
            <label for="pass">Password:</label>
            <input type="password" id="pass" name="pass" placeholder="Enter password">
            
            <button type="submit">Connect</button>
        </form>
        
        <div id="status"></div>
    </div>
    
    <script>
        // Scan for networks on load
        fetch('/scan')
            .then(r => r.json())
            .then(networks => {
                const container = document.getElementById('networks');
                if (networks.length === 0) {
                    container.innerHTML = '<p>No networks found. <a href="javascript:location.reload()">Retry</a></p>';
                    return;
                }
                container.innerHTML = networks.map(n => 
                    `<div class="network-item" onclick="selectNetwork('${n.ssid}')">
                        <span>${n.ssid}</span>
                        <span class="signal">${n.rssi} dBm ${n.secure ? '🔒' : ''}</span>
                    </div>`
                ).join('');
            })
            .catch(() => {
                document.getElementById('networks').innerHTML = '<p>Scan failed. <a href="javascript:location.reload()">Retry</a></p>';
            });
        
        function selectNetwork(ssid) {
            document.getElementById('ssid').value = ssid;
            document.getElementById('pass').focus();
        }
        
        document.getElementById('wifi-form').onsubmit = function(e) {
            e.preventDefault();
            const status = document.getElementById('status');
            status.innerHTML = '<div class="status"><div class="spinner"></div> Connecting...</div>';
            
            fetch('/connect', {
                method: 'POST',
                body: new FormData(this)
            })
            .then(r => r.json())
            .then(result => {
                if (result.success) {
                    status.innerHTML = '<div class="status success">✓ Connected! IP: ' + result.ip + '<br>Device will restart...</div>';
                    setTimeout(() => location.reload(), 5000);
                } else {
                    status.innerHTML = '<div class="status error">✗ ' + result.message + '</div>';
                }
            })
            .catch(() => {
                status.innerHTML = '<div class="status error">✗ Connection failed</div>';
            });
        };
    </script>
</body>
</html>
)rawliteral";

// Forward declarations
static void start_captive_portal();
static void setup_routes();

void wifi_manager_init() {
    preferences.begin(PREF_NAMESPACE, false);
    
    String saved_ssid = preferences.getString(PREF_SSID, "");
    String saved_pass = preferences.getString(PREF_PASS, "");
    
    if (saved_ssid.length() > 0) {
        // Try to connect to saved network
        Serial.printf("WiFi: Connecting to saved network: %s\n", saved_ssid.c_str());
        status_message = "Connecting to " + saved_ssid + "...";
        current_state = WIFI_STATE_CONNECTING;
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        connect_start_time = millis();
    } else {
        // No saved credentials, start AP mode
        Serial.println("WiFi: No saved credentials, starting AP mode");
        wifi_manager_start_ap();
    }
}

void wifi_manager_loop() {
    // Handle DNS for captive portal
    if (current_state == WIFI_STATE_AP_MODE) {
        dnsServer.processNextRequest();
    }
    
    // Handle connection state
    if (current_state == WIFI_STATE_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            current_state = WIFI_STATE_CONNECTED;
            status_message = "Connected: " + WiFi.localIP().toString();
            Serial.printf("WiFi: Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            
            // Start web server in station mode too (for Phase 2 admin UI)
            if (!server_started) {
                setup_routes();
                server.begin();
                server_started = true;
            }
        } else if (millis() - connect_start_time > CONNECT_TIMEOUT_MS) {
            Serial.println("WiFi: Connection timeout, starting AP mode");
            current_state = WIFI_STATE_FAILED;
            status_message = "Connection failed";
            wifi_manager_start_ap();
        }
    }
}

WiFiState wifi_manager_get_state() {
    return current_state;
}

const char* wifi_manager_get_status() {
    return status_message.c_str();
}

String wifi_manager_get_ip() {
    if (current_state == WIFI_STATE_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (current_state == WIFI_STATE_AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return "N/A";
}

void wifi_manager_start_ap() {
    Serial.println("WiFi: Starting AP mode...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    
    // Start DNS server for captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    
    current_state = WIFI_STATE_AP_MODE;
    status_message = String("AP: ") + AP_SSID;
    
    Serial.printf("WiFi: AP started - SSID: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    
    // Start web server
    if (!server_started) {
        start_captive_portal();
    }
}

bool wifi_manager_has_credentials() {
    String saved_ssid = preferences.getString(PREF_SSID, "");
    return saved_ssid.length() > 0;
}

void wifi_manager_clear_credentials() {
    preferences.remove(PREF_SSID);
    preferences.remove(PREF_PASS);
    Serial.println("WiFi: Credentials cleared");
}

static void setup_routes() {
    // Captive portal detection endpoints
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    
    // Main page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", CAPTIVE_PORTAL_HTML);
    });
    
    // Scan for networks
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "[";
        int n = WiFi.scanComplete();
        if (n == -2) {
            WiFi.scanNetworks(true);
        } else if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (i > 0) json += ",";
                json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
                json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
                json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN) + "}";
            }
            WiFi.scanDelete();
            WiFi.scanNetworks(true);  // Start new scan for next request
        }
        json += "]";
        request->send(200, "application/json", json);
    });
    
    // Connect to network
    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid = request->arg("ssid");
        String pass = request->arg("pass");
        
        if (ssid.length() == 0) {
            request->send(200, "application/json", "{\"success\":false,\"message\":\"SSID required\"}");
            return;
        }
        
        Serial.printf("WiFi: Attempting connection to: %s\n", ssid.c_str());
        
        // Save credentials
        preferences.putString(PREF_SSID, ssid);
        preferences.putString(PREF_PASS, pass);
        
        // Try to connect
        WiFi.mode(WIFI_AP_STA);  // Keep AP running while connecting
        WiFi.begin(ssid.c_str(), pass.c_str());
        
        // Wait for connection (with timeout)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            Serial.printf("WiFi: Connected! IP: %s\n", ip.c_str());
            request->send(200, "application/json", "{\"success\":true,\"ip\":\"" + ip + "\"}");
            
            // Schedule restart to apply clean state
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("WiFi: Connection failed");
            preferences.remove(PREF_SSID);
            preferences.remove(PREF_PASS);
            request->send(200, "application/json", "{\"success\":false,\"message\":\"Connection failed. Check password.\"}");
        }
    });
    
    // Status endpoint
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"state\":\"" + String(current_state) + "\",";
        json += "\"status\":\"" + status_message + "\",";
        json += "\"ip\":\"" + wifi_manager_get_ip() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI());
        json += "}";
        request->send(200, "application/json", json);
    });
}

static void start_captive_portal() {
    setup_routes();
    
    // Catch-all for captive portal
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    
    server.begin();
    server_started = true;
    Serial.println("WiFi: Captive portal started");
    
    // Start initial WiFi scan
    WiFi.scanNetworks(true);
}
