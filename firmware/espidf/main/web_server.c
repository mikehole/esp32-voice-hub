/**
 * Web Server - HTTP endpoints for control and OTA
 * 
 * Endpoints:
 *   GET  /admin              - Admin web interface
 *   GET  /api/status         - Device status JSON
 *   GET  /api/brightness     - Set brightness (?v=0-100)
 *   GET  /api/screenshot     - Capture display as BMP
 *   POST /api/play           - Play raw PCM audio
 *   POST /api/speak          - TTS (text in body, requires external TTS)
 *   POST /api/notify         - Show notification + speak
 *   POST /api/ota/upload     - Upload firmware binary
 *   POST /api/ota/url        - OTA from URL
 */

#include "web_server.h"
#include "ota_update.h"
#include "update_checker.h"
#include "voice_client.h"
#include "wifi_manager.h"
#include "config.h"
#include "audio.h"
#include "display.h"
#include "wakeword.h"
#include "notification.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "lvgl.h"

// MIN macro if not defined
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "web_srv";
static httpd_handle_t server = NULL;
static char auth_token[64] = {0};
static int64_t start_time = 0;

// ============================================================================
// Admin HTML Page
// ============================================================================

static const char ADMIN_HTML[] = 
"<!DOCTYPE html><html><head>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Voice Hub Admin</title>"
"<style>"
"*{box-sizing:border-box}"
"body{font-family:sans-serif;background:#0A1929;color:#5DADE2;margin:0;padding:20px}"
".c{max-width:500px;margin:0 auto;background:#0F2744;border-radius:15px;padding:20px;border:2px solid #2E86AB}"
"h1{text-align:center;margin-top:0}"
".l{text-align:center;font-size:40px}"
".row{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #2E86AB}"
".row:last-child{border-bottom:none}"
".label{color:#85C1E9}"
".value{font-weight:bold}"
"button{padding:10px 20px;background:#5DADE2;color:#0A1929;border:none;border-radius:8px;font-weight:bold;cursor:pointer;margin:5px}"
"button.danger{background:#E74C3C}"
"button.success{background:#27AE60}"
"input[type=range]{width:100%}"
".section{margin-top:20px;padding-top:15px;border-top:2px solid #2E86AB}"
"#screen{max-width:100%;border-radius:10px;margin-top:10px}"
".update-box{background:#1a3a5c;padding:10px;border-radius:8px;margin-top:10px}"
"</style></head><body>"
"<div class=\"c\">"
"<div class=\"l\">&#129417;</div>"
"<h1>Voice Hub Admin</h1>"
"<div id=\"status\">Loading...</div>"
"<div class=\"section\">"
"<h3>Firmware Update</h3>"
"<div id=\"updateStatus\">Click to check for updates</div>"
"<button onclick=\"checkUpdate()\">Check for Updates</button>"
"<button id=\"installBtn\" class=\"success\" style=\"display:none\" onclick=\"installUpdate()\">Install Update</button>"
"<div id=\"updateInfo\" class=\"update-box\" style=\"display:none\"></div>"
"</div>"
"<div class=\"section\">"
"<h3>Display</h3>"
"<label>Brightness: <span id=\"bval\">100</span>%</label>"
"<input type=\"range\" id=\"brightness\" min=\"10\" max=\"100\" value=\"100\" onchange=\"setBrightness(this.value)\">"
"<br><button onclick=\"screenshot()\">Take Screenshot</button>"
"<div id=\"screenwrap\"></div>"
"</div>"
"<div class=\"section\">"
"<h3>Audio Test</h3>"
"<button onclick=\"testSpeak()\">Test TTS</button>"
"<span id=\"audioStatus\"></span>"
"</div>"
"<div class=\"section\">"
"<h3>System</h3>"
"<button onclick=\"restart()\">Restart Device</button>"
"</div>"
"</div>"
"<script>"
"var updateUrl='';"
"function load(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"var h='';"
"h+='<div class=\"row\"><span class=\"label\">IP Address</span><span class=\"value\">'+d.ip+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Version</span><span class=\"value\">'+d.version+' ('+d.sha+')</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Uptime</span><span class=\"value\">'+d.uptime+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Free Heap</span><span class=\"value\">'+d.heap+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Free PSRAM</span><span class=\"value\">'+d.psram+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">WiFi Signal</span><span class=\"value\">'+d.rssi+' dBm</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Wake Word</span><span class=\"value\">'+(d.wakeword?'Enabled':'Disabled')+'</span></div>';"
"document.getElementById('status').innerHTML=h;"
"document.getElementById('brightness').value=d.brightness;"
"document.getElementById('bval').innerText=d.brightness;"
"});"
"}"
"function checkUpdate(){"
"document.getElementById('updateStatus').innerText='Checking...';"
"document.getElementById('installBtn').style.display='none';"
"fetch('/api/ota/check').then(r=>r.json()).then(d=>{"
"if(d.available){"
"document.getElementById('updateStatus').innerHTML='<strong>Update available!</strong>';"
"document.getElementById('updateInfo').style.display='block';"
"document.getElementById('updateInfo').innerHTML='New version: '+d.version+'<br>Current: '+d.current;"
"document.getElementById('installBtn').style.display='inline-block';"
"updateUrl=d.url;"
"}else{"
"document.getElementById('updateStatus').innerText='Up to date ('+d.current+')';"
"document.getElementById('updateInfo').style.display='none';"
"}"
"}).catch(e=>{"
"document.getElementById('updateStatus').innerText='Check failed: '+e;"
"});"
"}"
"function installUpdate(){"
"if(!updateUrl){alert('No update URL');return;}"
"if(!confirm('Install firmware update? Device will restart.')){return;}"
"document.getElementById('updateStatus').innerText='Installing... (this takes ~1 minute)';"
"document.getElementById('installBtn').style.display='none';"
"fetch('/api/ota/url',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url:updateUrl})}).then(r=>{"
"document.getElementById('updateStatus').innerText='Update started, device restarting...';"
"setTimeout(()=>location.reload(),60000);"
"}).catch(e=>{"
"document.getElementById('updateStatus').innerText='Install failed: '+e;"
"});"
"}"
"function setBrightness(v){"
"document.getElementById('bval').innerText=v;"
"fetch('/api/brightness?v='+v);"
"}"
"function screenshot(){"
"document.getElementById('screenwrap').innerHTML='<p>Capturing...</p>';"
"fetch('/api/screenshot').then(r=>r.blob()).then(b=>{"
"var url=URL.createObjectURL(b);"
"document.getElementById('screenwrap').innerHTML='<img id=\"screen\" src=\"'+url+'\">';"
"});"
"}"
"function testSpeak(){"
"document.getElementById('audioStatus').innerText=' Playing...';"
"fetch('/api/notify',{method:'POST',body:'Hello from the admin panel!'}).then(()=>{"
"document.getElementById('audioStatus').innerText=' Done';"
"setTimeout(()=>{document.getElementById('audioStatus').innerText='';},2000);"
"});"
"}"
"function restart(){"
"if(confirm('Restart device?')){"
"fetch('/api/restart').then(()=>alert('Restarting...'));"
"}"
"}"
"load();setInterval(load,5000);"
"</script></body></html>";

// ============================================================================
// Setup Portal HTML (Captive Portal)
// ============================================================================

static const char SETUP_HTML[] = 
"<!DOCTYPE html><html><head>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Voice Hub Setup</title>"
"<style>"
"*{box-sizing:border-box}"
"body{font-family:sans-serif;background:#0A1929;color:#5DADE2;margin:0;padding:20px}"
".c{max-width:400px;margin:0 auto;background:#0F2744;border-radius:15px;padding:20px;border:2px solid #2E86AB}"
"h1,h2{text-align:center;margin-top:0}"
".l{text-align:center;font-size:50px}"
"label{display:block;margin:15px 0 5px;color:#85C1E9}"
"input,select{width:100%;padding:12px;border:2px solid #2E86AB;border-radius:8px;background:#1a3a5c;color:#fff;font-size:16px}"
"input:focus,select:focus{outline:none;border-color:#5DADE2}"
"button{width:100%;padding:15px;background:#5DADE2;color:#0A1929;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:15px}"
"button:hover{background:#85C1E9}"
"button:disabled{background:#2E86AB;cursor:not-allowed}"
".step{display:none}.step.active{display:block}"
".status{text-align:center;padding:10px;margin:10px 0;border-radius:8px}"
".status.ok{background:#27AE60;color:#fff}"
".status.err{background:#E74C3C;color:#fff}"
".status.info{background:#2E86AB;color:#fff}"
".networks{max-height:200px;overflow-y:auto;margin:10px 0}"
".net{padding:10px;background:#1a3a5c;margin:5px 0;border-radius:8px;cursor:pointer;display:flex;justify-content:space-between}"
".net:hover{background:#2E86AB}"
".net .rssi{color:#85C1E9;font-size:12px}"
".skip{text-align:center;margin-top:15px}"
".skip a{color:#85C1E9;cursor:pointer}"
"</style></head><body>"
"<div class=\"c\">"
"<div class=\"l\">&#129417;</div>"
"<h1>Voice Hub Setup</h1>"

// Step 1: WiFi
"<div id=\"step1\" class=\"step active\">"
"<h2>1. Connect to WiFi</h2>"
"<div id=\"scanStatus\" class=\"status info\">Scanning for networks...</div>"
"<div id=\"networks\" class=\"networks\"></div>"
"<label>Network Name (SSID)</label>"
"<input type=\"text\" id=\"ssid\" placeholder=\"Select or enter network name\">"
"<label>Password</label>"
"<input type=\"password\" id=\"password\" placeholder=\"WiFi password\">"
"<button onclick=\"connectWifi()\">Connect</button>"
"<div id=\"wifiStatus\"></div>"
"</div>"

// Step 2: OpenClaw
"<div id=\"step2\" class=\"step\">"
"<h2>2. Configure OpenClaw</h2>"
"<p style=\"color:#85C1E9;font-size:14px\">Enter your OpenClaw WebSocket URL. This is where voice requests will be sent.</p>"
"<label>OpenClaw URL</label>"
"<input type=\"text\" id=\"ocUrl\" placeholder=\"ws://192.168.1.100:8765\">"
"<label>Token (optional)</label>"
"<input type=\"password\" id=\"ocToken\" placeholder=\"Authentication token\">"
"<button onclick=\"saveOpenClaw()\">Save & Finish</button>"
"<div class=\"skip\"><a onclick=\"skipOpenClaw()\">Skip for now</a></div>"
"<div id=\"ocStatus\"></div>"
"</div>"

// Step 3: Done
"<div id=\"step3\" class=\"step\">"
"<h2>&#10003; Setup Complete!</h2>"
"<p style=\"text-align:center\">Your Voice Hub is configured and ready to use.</p>"
"<div id=\"finalIp\" class=\"status ok\"></div>"
"<p style=\"text-align:center;color:#85C1E9\">Say \"Hi ESP\" or tap the screen to start talking.</p>"
"<button onclick=\"location.href='/admin'\">Open Admin Panel</button>"
"</div>"

"</div>"
"<script>"
"function showStep(n){"
"document.querySelectorAll('.step').forEach(s=>s.classList.remove('active'));"
"document.getElementById('step'+n).classList.add('active');"
"}"
"function scan(){"
"fetch('/api/wifi/scan').then(r=>r.json()).then(nets=>{"
"document.getElementById('scanStatus').style.display='none';"
"var h='';"
"nets.forEach(n=>{"
"var bars=n.rssi>-50?'\\u2587\\u2587\\u2587':n.rssi>-70?'\\u2587\\u2587':'\\u2587';"
"h+='<div class=\"net\" onclick=\"selectNet(\\''+n.ssid+'\\')\"><span>'+n.ssid+(n.secure?' \\uD83D\\uDD12':'')+'</span><span class=\"rssi\">'+bars+' '+n.rssi+'</span></div>';"
"});"
"document.getElementById('networks').innerHTML=h||'<div class=\"status info\">No networks found</div>';"
"}).catch(e=>{"
"document.getElementById('scanStatus').className='status err';"
"document.getElementById('scanStatus').innerText='Scan failed: '+e;"
"});"
"}"
"function selectNet(ssid){"
"document.getElementById('ssid').value=ssid;"
"document.getElementById('password').focus();"
"}"
"function connectWifi(){"
"var ssid=document.getElementById('ssid').value;"
"var pass=document.getElementById('password').value;"
"if(!ssid){alert('Enter network name');return;}"
"document.getElementById('wifiStatus').innerHTML='<div class=\"status info\">Connecting...</div>';"
"fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})}).then(r=>r.json()).then(d=>{"
"if(d.success){"
"document.getElementById('wifiStatus').innerHTML='<div class=\"status ok\">Connected! IP: '+d.ip+'</div>';"
"setTimeout(()=>showStep(2),1500);"
"}else{"
"document.getElementById('wifiStatus').innerHTML='<div class=\"status err\">Failed: '+d.error+'</div>';"
"}"
"}).catch(e=>{"
"document.getElementById('wifiStatus').innerHTML='<div class=\"status err\">Error: '+e+'</div>';"
"});"
"}"
"function saveOpenClaw(){"
"var url=document.getElementById('ocUrl').value;"
"var token=document.getElementById('ocToken').value;"
"if(!url){alert('Enter OpenClaw URL');return;}"
"fetch('/api/config/openclaw',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url:url,token:token})}).then(r=>r.json()).then(d=>{"
"if(d.success){"
"document.getElementById('finalIp').innerText='Device IP: '+d.ip;"
"showStep(3);"
"}else{"
"document.getElementById('ocStatus').innerHTML='<div class=\"status err\">Failed: '+d.error+'</div>';"
"}"
"});"
"}"
"function skipOpenClaw(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"document.getElementById('finalIp').innerText='Device IP: '+d.ip;"
"showStep(3);"
"});"
"}"
"scan();"
"</script></body></html>";

// ============================================================================
// Helpers
// ============================================================================

static bool check_auth(httpd_req_t *req)
{
    if (strlen(auth_token) == 0) {
        return true;  // No token configured, allow all
    }
    
    char auth_header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) == ESP_OK) {
        // Check "Bearer <token>" format
        if (strncmp(auth_header, "Bearer ", 7) == 0) {
            if (strcmp(auth_header + 7, auth_token) == 0) {
                return true;
            }
        }
    }
    
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "Unauthorized", -1);
    return false;
}

// ============================================================================
// Handlers
// ============================================================================

// GET / or /setup - Setup portal (captive portal landing page)
static esp_err_t setup_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, strlen(SETUP_HTML));
    return ESP_OK;
}

// Captive portal detection endpoints
static esp_err_t captive_handler(httpd_req_t *req)
{
    // Redirect to setup page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// GET /api/wifi/scan - Scan for WiFi networks
static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    char *json = wifi_manager_scan();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// POST /api/wifi/connect - Connect to WiFi network
static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }
    
    cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_json = cJSON_GetObjectItem(root, "password");
    
    if (!ssid_json || !cJSON_IsString(ssid_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_OK;
    }
    
    const char *ssid = ssid_json->valuestring;
    const char *password = pass_json && cJSON_IsString(pass_json) ? pass_json->valuestring : "";
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    
    // Start connection (this saves credentials and switches from AP to STA mode)
    esp_err_t err = wifi_manager_connect(ssid, password);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        // Wait a bit for connection
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        if (wifi_manager_get_state() == WIFI_STATE_CONNECTED) {
            cJSON_AddBoolToObject(response, "success", true);
            cJSON_AddStringToObject(response, "ip", wifi_manager_get_ip());
        } else {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "error", "Connection timeout");
            // Go back to AP mode
            wifi_manager_start_ap();
        }
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Failed to start connection");
    }
    
    char *json = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/config/openclaw - Configure OpenClaw endpoint
static esp_err_t config_openclaw_handler(httpd_req_t *req)
{
    char buf[512] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }
    
    cJSON *url_json = cJSON_GetObjectItem(root, "url");
    cJSON *token_json = cJSON_GetObjectItem(root, "token");
    
    const char *url = url_json && cJSON_IsString(url_json) ? url_json->valuestring : "";
    const char *token = token_json && cJSON_IsString(token_json) ? token_json->valuestring : "";
    
    config_set_openclaw(url, token);
    
    // Tell voice client to reconnect with new URL
    if (strlen(url) > 0) {
        voice_client_connect(url);
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "ip", wifi_manager_get_ip());
    
    char *json = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return ESP_OK;
}

// GET /api/config - Get current configuration
static esp_err_t config_get_handler(httpd_req_t *req)
{
    const config_t *cfg = config_get();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);
    cJSON_AddStringToObject(root, "openclaw_url", cfg->openclaw_url);
    cJSON_AddBoolToObject(root, "openclaw_configured", config_has_openclaw());
    cJSON_AddNumberToObject(root, "brightness", cfg->brightness);
    cJSON_AddBoolToObject(root, "wakeword", cfg->wakeword_enabled);
    cJSON_AddNumberToObject(root, "volume", cfg->volume);
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// GET /admin - Admin web interface
static esp_err_t admin_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ADMIN_HTML, strlen(ADMIN_HTML));
    return ESP_OK;
}

// GET /api/status - Device status (enhanced)
static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", "esp32-voice-hub");
    cJSON_AddStringToObject(root, "version", ota_get_version());
    cJSON_AddStringToObject(root, "sha", ota_get_sha256_short());
    cJSON_AddStringToObject(root, "ip", wifi_manager_get_ip());
    cJSON_AddStringToObject(root, "framework", "esp-idf");
    
    // Memory info
    char heap_str[32], psram_str[32];
    snprintf(heap_str, sizeof(heap_str), "%u KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    snprintf(psram_str, sizeof(psram_str), "%u KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    cJSON_AddStringToObject(root, "heap", heap_str);
    cJSON_AddStringToObject(root, "psram", psram_str);
    
    // Uptime
    int64_t uptime_us = esp_timer_get_time() - start_time;
    int secs = (uptime_us / 1000000) % 60;
    int mins = (uptime_us / 60000000) % 60;
    int hrs = uptime_us / 3600000000;
    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "%dh %dm %ds", hrs, mins, secs);
    cJSON_AddStringToObject(root, "uptime", uptime_str);
    
    // WiFi RSSI
    wifi_ap_record_t ap_info;
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    cJSON_AddNumberToObject(root, "rssi", rssi);
    
    // Brightness (0-255 -> 0-100%)
    int brightness = (display_get_brightness() * 100) / 255;
    cJSON_AddNumberToObject(root, "brightness", brightness);
    
    // Wake word status
    cJSON_AddBoolToObject(root, "wakeword", wakeword_is_running());
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// GET /api/brightness - Set display brightness
static esp_err_t brightness_handler(httpd_req_t *req)
{
    char query[32] = {0};
    char val[8] = {0};
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "v", val, sizeof(val));
    }
    
    int brightness = atoi(val);
    if (brightness >= 0 && brightness <= 100) {
        display_set_brightness((brightness * 255) / 100);
        ESP_LOGI(TAG, "Brightness set to %d%%", brightness);
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// POST /api/notify - Queue notification (user must tap to acknowledge and hear it)
// Query params: silent=1 to skip attention chime
static esp_err_t notify_handler(httpd_req_t *req)
{
    // Check for silent param
    bool silent = false;
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(query, "silent", param, sizeof(param)) == ESP_OK) {
            silent = (strcmp(param, "1") == 0 || strcmp(param, "true") == 0);
        }
    }
    
    // Read message from body (max 1024 chars to match notification buffer)
    char buf[1024] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received > 0) {
        buf[received] = '\0';
        ESP_LOGI(TAG, "Notify (silent=%d): %s", silent, buf);
        
        // Queue the notification - will show avatar and play attention sound
        if (notification_queue_ex(buf, silent)) {
            // Set display to notification state (shows notification avatar + purple ring)
            display_set_state(DISPLAY_STATE_NOTIFICATION);
            httpd_resp_send(req, "Notification queued - tap to acknowledge", -1);
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to queue notification");
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No message body");
    }
    
    return ESP_OK;
}

// POST /api/notify-audio - Queue pre-rendered audio notification
// Query params: rate=<sample_rate> (default 24000), silent=1
// Body: raw PCM audio data (16-bit signed mono)
static esp_err_t notify_audio_handler(httpd_req_t *req)
{
    // Parse query params
    uint32_t sample_rate = 24000;
    bool silent = false;
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "rate", param, sizeof(param)) == ESP_OK) {
            sample_rate = atoi(param);
            if (sample_rate < 8000 || sample_rate > 48000) sample_rate = 24000;
        }
        if (httpd_query_key_value(query, "silent", param, sizeof(param)) == ESP_OK) {
            silent = (strcmp(param, "1") == 0 || strcmp(param, "true") == 0);
        }
    }
    
    // Check content length
    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > NOTIFICATION_MAX_AUDIO_SIZE) {
        ESP_LOGW(TAG, "Invalid audio size: %u", content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid audio size (max 512KB)");
        return ESP_OK;
    }
    
    // Allocate buffer in PSRAM
    uint8_t *audio_buf = heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    if (!audio_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }
    
    // Read audio data
    size_t received = 0;
    while (received < content_len) {
        int ret = httpd_req_recv(req, (char*)(audio_buf + received), content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Failed to receive audio data");
            heap_caps_free(audio_buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_OK;
        }
        received += ret;
    }
    
    ESP_LOGI(TAG, "Received %u bytes audio @ %u Hz (silent=%d)", received, sample_rate, silent);
    
    // Queue the notification (copies the data)
    if (notification_queue_audio_ex(audio_buf, received, sample_rate, NULL, silent)) {
        display_set_state(DISPLAY_STATE_NOTIFICATION);
        httpd_resp_send(req, "Audio notification queued - tap to acknowledge", -1);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to queue notification");
    }
    
    heap_caps_free(audio_buf);
    return ESP_OK;
}

// GET /api/restart - Restart device
static esp_err_t restart_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "Restarting...", -1);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Synchronous update check (blocking)
static bool check_done = false;
static bool check_result = false;
static char check_version[32] = {0};

static void on_check_complete(bool available, const char* version) {
    check_result = available;
    if (version) {
        strncpy(check_version, version, sizeof(check_version) - 1);
    }
    check_done = true;
}

// GET /api/ota/check - Check for firmware updates
static esp_err_t ota_check_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Checking for updates via web API");
    
    // Pause wakeword during check
    bool was_running = wakeword_is_running();
    if (was_running) {
        wakeword_stop();
    }
    
    // Start check
    check_done = false;
    check_result = false;
    check_version[0] = '\0';
    update_checker_check(on_check_complete);
    
    // Wait for completion (with timeout)
    int timeout = 100;  // 10 seconds max
    while (!check_done && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Resume wakeword
    if (was_running) {
        wakeword_start();
    }
    
    // Build response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "available", check_result);
    cJSON_AddStringToObject(root, "version", check_version);
    cJSON_AddStringToObject(root, "current", ota_get_sha256_short());
    cJSON_AddStringToObject(root, "url", update_checker_get_url());
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// GET /api/ota/mode - Enter OTA mode (pause wakeword) - legacy
static esp_err_t ota_mode_handler(httpd_req_t *req)
{
    if (wakeword_is_running()) {
        wakeword_stop();
        ESP_LOGI(TAG, "OTA mode: wakeword paused");
    }
    httpd_resp_send(req, "OTA mode enabled", -1);
    return ESP_OK;
}

// POST /api/ota/upload - Direct firmware upload
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return ESP_OK;
    
    ESP_LOGI(TAG, "OTA upload started, size: %d", req->content_len);
    
    esp_err_t err = ota_begin();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "OTA begin failed", -1);
        return ESP_OK;
    }
    
    // Read and write in chunks (larger buffer, retry on EAGAIN)
    char buf[4096];  // Larger buffer for efficiency
    int remaining = req->content_len;
    int received;
    int retry_count;
    
    while (remaining > 0) {
        retry_count = 0;
        do {
            received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
            if (received == HTTPD_SOCK_ERR_TIMEOUT || received == 0) {
                // EAGAIN/timeout - retry with small delay
                vTaskDelay(pdMS_TO_TICKS(10));
                retry_count++;
            }
        } while ((received == HTTPD_SOCK_ERR_TIMEOUT || received == 0) && retry_count < 100);
        
        if (received <= 0) {
            ESP_LOGE(TAG, "Receive failed after %d retries", retry_count);
            ota_abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Receive failed", -1);
            return ESP_OK;
        }
        
        err = ota_write((uint8_t*)buf, received);
        if (err != ESP_OK) {
            ota_abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "OTA write failed", -1);
            return ESP_OK;
        }
        
        remaining -= received;
    }
    
    ESP_LOGI(TAG, "OTA upload complete, finalizing...");
    
    // This will restart the device on success
    httpd_resp_send(req, "OTA complete, restarting...", -1);
    
    // Small delay to let response send
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ota_end();  // This restarts
    
    return ESP_OK;
}

// POST /api/ota/url - Update from URL
static esp_err_t ota_url_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return ESP_OK;
    
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "No body", -1);
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Invalid JSON", -1);
        return ESP_OK;
    }
    
    cJSON *url = cJSON_GetObjectItem(json, "url");
    if (!url || !cJSON_IsString(url)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing url field", -1);
        return ESP_OK;
    }
    
    httpd_resp_send(req, "Starting OTA from URL...", -1);
    
    // Small delay then start OTA (will restart on success)
    vTaskDelay(pdMS_TO_TICKS(100));
    ota_update_from_url(url->valuestring);
    
    cJSON_Delete(json);
    return ESP_OK;
}

// GET /api/record?duration=N - Record audio from mic (returns raw PCM)
static esp_err_t record_handler(httpd_req_t *req)
{
    // Parse query params for duration
    char query[64] = {0};
    int duration_ms = 3000;  // Default 3 seconds
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "duration", param, sizeof(param)) == ESP_OK) {
            duration_ms = atoi(param);
            if (duration_ms < 100) duration_ms = 100;
            if (duration_ms > 10000) duration_ms = 10000;
        }
    }
    
    ESP_LOGI(TAG, "Recording %d ms", duration_ms);
    
    // Calculate buffer size (16kHz, 16-bit mono = 32KB/sec)
    size_t buffer_size = (16000 * 2 * duration_ms) / 1000;
    uint8_t *audio_data = malloc(buffer_size);
    if (!audio_data) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Out of memory", -1);
        return ESP_OK;
    }
    
    if (!audio_start_recording(buffer_size)) {
        free(audio_data);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Recording failed to start", -1);
        return ESP_OK;
    }
    
    // Record audio in chunks
    size_t total_recorded = 0;
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(duration_ms + 500);
    
    while (total_recorded < buffer_size) {
        size_t chunk = audio_record_chunk(audio_data + total_recorded, 
                                          MIN(4096, buffer_size - total_recorded));
        if (chunk > 0) {
            total_recorded += chunk;
        }
        
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    audio_stop_recording();
    
    ESP_LOGI(TAG, "Recorded %u bytes", total_recorded);
    
    // Send audio data
    httpd_resp_set_type(req, "audio/raw");
    httpd_resp_set_hdr(req, "X-Sample-Rate", "16000");
    httpd_resp_set_hdr(req, "X-Bits-Per-Sample", "16");
    httpd_resp_set_hdr(req, "X-Channels", "1");
    httpd_resp_send(req, (const char*)audio_data, total_recorded);
    
    free(audio_data);
    return ESP_OK;
}

// POST /api/play - Play audio (for TTS responses)
static esp_err_t play_handler(httpd_req_t *req)
{
    // Parse query params for sample rate
    char query[64] = {0};
    uint32_t sample_rate = 24000;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "rate", param, sizeof(param)) == ESP_OK) {
            sample_rate = atoi(param);
        }
    }
    
    ESP_LOGI(TAG, "Play: %d bytes at %lu Hz", req->content_len, sample_rate);
    
    // Allocate buffer for audio
    uint8_t *audio_data = malloc(req->content_len);
    if (!audio_data) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Out of memory", -1);
        return ESP_OK;
    }
    
    // Receive audio data
    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char*)audio_data + offset, remaining);
        if (received <= 0) {
            free(audio_data);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Receive failed", -1);
            return ESP_OK;
        }
        offset += received;
        remaining -= received;
    }
    
    // Play audio (audio module will free the buffer)
    if (audio_play(audio_data, req->content_len, sample_rate, true)) {
        httpd_resp_send(req, "Playing", -1);
    } else {
        free(audio_data);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Playback failed", -1);
    }
    return ESP_OK;
}

// ============================================================================
// Screenshot handler
// ============================================================================

static esp_err_t screenshot_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Screenshot request");
    
    // Pause wakeword - it can interfere
    bool was_wakeword = wakeword_is_running();
    if (was_wakeword) {
        wakeword_stop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Start capture
    if (!display_screenshot_start()) {
        if (was_wakeword) wakeword_start();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start capture");
        return ESP_FAIL;
    }
    
    // Wait for capture to complete (LVGL will fill buffer during normal refresh)
    int timeout = 50;  // 50 * 20ms = 1 second
    while (!display_screenshot_complete() && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    if (!display_screenshot_complete()) {
        display_screenshot_free();
        if (was_wakeword) wakeword_start();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture timeout");
        return ESP_FAIL;
    }
    
    uint16_t *rgb_buf = display_screenshot_get_buffer();
    if (!rgb_buf) {
        display_screenshot_free();
        if (was_wakeword) wakeword_start();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No buffer");
        return ESP_FAIL;
    }
    
    // Swap bytes for BMP format
    size_t pixels = 360 * 360;
    for (size_t i = 0; i < pixels; i++) {
        uint16_t px = rgb_buf[i];
        rgb_buf[i] = (px >> 8) | (px << 8);
    }
    
    // Create BMP header
    uint8_t bmp_header[70];
    memset(bmp_header, 0, sizeof(bmp_header));
    
    bmp_header[0] = 'B'; bmp_header[1] = 'M';
    uint32_t file_size = 70 + pixels * 2;
    memcpy(&bmp_header[2], &file_size, 4);
    uint32_t offset = 70;
    memcpy(&bmp_header[10], &offset, 4);
    
    uint32_t dib_size = 56;
    memcpy(&bmp_header[14], &dib_size, 4);
    int32_t width = 360, height = -360;
    memcpy(&bmp_header[18], &width, 4);
    memcpy(&bmp_header[22], &height, 4);
    uint16_t planes = 1, bpp = 16;
    memcpy(&bmp_header[26], &planes, 2);
    memcpy(&bmp_header[28], &bpp, 2);
    uint32_t compression = 3;
    memcpy(&bmp_header[30], &compression, 4);
    uint32_t img_size = pixels * 2;
    memcpy(&bmp_header[34], &img_size, 4);
    
    uint32_t r_mask = 0xF800, g_mask = 0x07E0, b_mask = 0x001F;
    memcpy(&bmp_header[54], &r_mask, 4);
    memcpy(&bmp_header[58], &g_mask, 4);
    memcpy(&bmp_header[62], &b_mask, 4);
    
    // Send
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_send_chunk(req, (char*)bmp_header, 70);
    httpd_resp_send_chunk(req, (char*)rgb_buf, pixels * 2);
    httpd_resp_send_chunk(req, NULL, 0);
    
    display_screenshot_free();
    
    if (was_wakeword) {
        wakeword_start();
    }
    
    ESP_LOGI(TAG, "Screenshot sent");
    return ESP_OK;
}

#if 0  // Disabled until we fix the crash
static esp_err_t screenshot_handler_DISABLED(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Screenshot request");
    
    // Try to get display lock with retries
    bool got_lock = false;
    for (int i = 0; i < 10; i++) {
        if (display_lock(100)) {
            got_lock = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    if (!got_lock) {
        ESP_LOGE(TAG, "Screenshot: couldn't get display lock");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Display busy");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Screenshot: got lock, taking snapshot");
    
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t w = lv_obj_get_width(scr);
    lv_coord_t h = lv_obj_get_height(scr);
    
    ESP_LOGI(TAG, "Screenshot: %dx%d", w, h);
    
    // Take LVGL snapshot
    lv_img_dsc_t *snapshot = lv_snapshot_take(scr, LV_IMG_CF_TRUE_COLOR);
    display_unlock();
    
    ESP_LOGI(TAG, "Screenshot: snapshot taken, lock released");
    
    if (!snapshot) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot failed");
        return ESP_FAIL;
    }
    
    // BMP header (54 bytes) + pixel data
    uint32_t row_size = ((w * 3 + 3) / 4) * 4;  // Rows padded to 4 bytes
    uint32_t pixel_size = row_size * h;
    uint32_t file_size = 54 + pixel_size;
    
    // Allocate BMP buffer in PSRAM
    uint8_t *bmp = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (!bmp) {
        lv_snapshot_free(snapshot);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    // BMP file header
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = file_size & 0xFF;
    bmp[3] = (file_size >> 8) & 0xFF;
    bmp[4] = (file_size >> 16) & 0xFF;
    bmp[5] = (file_size >> 24) & 0xFF;
    bmp[6] = bmp[7] = bmp[8] = bmp[9] = 0;  // Reserved
    bmp[10] = 54; bmp[11] = bmp[12] = bmp[13] = 0;  // Pixel data offset
    
    // DIB header (BITMAPINFOHEADER)
    bmp[14] = 40; bmp[15] = bmp[16] = bmp[17] = 0;  // Header size
    bmp[18] = w & 0xFF; bmp[19] = (w >> 8) & 0xFF; bmp[20] = bmp[21] = 0;  // Width
    bmp[22] = h & 0xFF; bmp[23] = (h >> 8) & 0xFF; bmp[24] = bmp[25] = 0;  // Height
    bmp[26] = 1; bmp[27] = 0;  // Planes
    bmp[28] = 24; bmp[29] = 0;  // Bits per pixel
    memset(&bmp[30], 0, 24);  // Compression, size, resolution, colors
    
    // Convert RGB565 to BGR24 (BMP format, bottom-up)
    uint16_t *src = (uint16_t*)snapshot->data;
    for (int y = 0; y < h; y++) {
        uint8_t *row = &bmp[54 + (h - 1 - y) * row_size];  // Bottom-up
        for (int x = 0; x < w; x++) {
            uint16_t pixel = src[y * w + x];
            // RGB565 -> BGR24
            #if LV_COLOR_16_SWAP
            pixel = ((pixel & 0xFF) << 8) | ((pixel >> 8) & 0xFF);
            #endif
            uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            uint8_t b = (pixel & 0x1F) << 3;
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }
    
    // Send response
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_send(req, (char*)bmp, file_size);
    
    // Cleanup
    heap_caps_free(bmp);
    lv_snapshot_free(snapshot);
    
    return ESP_OK;
}
#endif  // Disabled screenshot handler

// ============================================================================
// Server lifecycle
// ============================================================================

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    
    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return err;
    }
    
    // Initialize start time for uptime
    start_time = esp_timer_get_time();
    
    // Register endpoints
    
    // Setup portal (captive portal)
    httpd_uri_t setup_uri = {
        .uri = "/setup",
        .method = HTTP_GET,
        .handler = setup_handler
    };
    httpd_register_uri_handler(server, &setup_uri);
    
    // Root redirects to setup in AP mode, admin otherwise
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = wifi_manager_is_ap_mode() ? setup_handler : admin_handler
    };
    httpd_register_uri_handler(server, &root_uri);
    
    // Captive portal detection (various OS probes)
    httpd_uri_t captive_uris[] = {
        {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/redirect", .method = HTTP_GET, .handler = captive_handler},
    };
    for (int i = 0; i < sizeof(captive_uris)/sizeof(captive_uris[0]); i++) {
        httpd_register_uri_handler(server, &captive_uris[i]);
    }
    
    // WiFi config endpoints
    httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_handler
    };
    httpd_register_uri_handler(server, &wifi_scan_uri);
    
    httpd_uri_t wifi_connect_uri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = wifi_connect_handler
    };
    httpd_register_uri_handler(server, &wifi_connect_uri);
    
    // Config endpoints
    httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler
    };
    httpd_register_uri_handler(server, &config_get_uri);
    
    httpd_uri_t config_openclaw_uri = {
        .uri = "/api/config/openclaw",
        .method = HTTP_POST,
        .handler = config_openclaw_handler
    };
    httpd_register_uri_handler(server, &config_openclaw_uri);
    
    httpd_uri_t admin_uri = {
        .uri = "/admin",
        .method = HTTP_GET,
        .handler = admin_handler
    };
    httpd_register_uri_handler(server, &admin_uri);
    
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler
    };
    httpd_register_uri_handler(server, &status_uri);
    
    httpd_uri_t brightness_uri = {
        .uri = "/api/brightness",
        .method = HTTP_GET,
        .handler = brightness_handler
    };
    httpd_register_uri_handler(server, &brightness_uri);
    
    httpd_uri_t notify_uri = {
        .uri = "/api/notify",
        .method = HTTP_POST,
        .handler = notify_handler
    };
    httpd_register_uri_handler(server, &notify_uri);
    
    httpd_uri_t notify_audio_uri = {
        .uri = "/api/notify-audio",
        .method = HTTP_POST,
        .handler = notify_audio_handler
    };
    httpd_register_uri_handler(server, &notify_audio_uri);
    
    httpd_uri_t restart_uri = {
        .uri = "/api/restart",
        .method = HTTP_GET,
        .handler = restart_handler
    };
    httpd_register_uri_handler(server, &restart_uri);
    
    httpd_uri_t ota_check_uri = {
        .uri = "/api/ota/check",
        .method = HTTP_GET,
        .handler = ota_check_handler
    };
    httpd_register_uri_handler(server, &ota_check_uri);
    
    httpd_uri_t ota_mode_uri = {
        .uri = "/api/ota/mode",
        .method = HTTP_GET,
        .handler = ota_mode_handler
    };
    httpd_register_uri_handler(server, &ota_mode_uri);
    
    httpd_uri_t ota_upload_uri = {
        .uri = "/api/ota/upload",
        .method = HTTP_POST,
        .handler = ota_upload_handler
    };
    httpd_register_uri_handler(server, &ota_upload_uri);
    
    httpd_uri_t ota_url_uri = {
        .uri = "/api/ota/url",
        .method = HTTP_POST,
        .handler = ota_url_handler
    };
    httpd_register_uri_handler(server, &ota_url_uri);
    
    httpd_uri_t play_uri = {
        .uri = "/api/play",
        .method = HTTP_POST,
        .handler = play_handler
    };
    httpd_register_uri_handler(server, &play_uri);
    
    httpd_uri_t record_uri = {
        .uri = "/api/record",
        .method = HTTP_GET,
        .handler = record_handler
    };
    httpd_register_uri_handler(server, &record_uri);
    
    httpd_uri_t screenshot_uri = {
        .uri = "/api/screenshot",
        .method = HTTP_GET,
        .handler = screenshot_handler
    };
    httpd_register_uri_handler(server, &screenshot_uri);
    
    ESP_LOGI(TAG, "Server started with %d endpoints", 11);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Server stopped");
    }
}

void web_server_set_token(const char* token)
{
    strncpy(auth_token, token, sizeof(auth_token) - 1);
    ESP_LOGI(TAG, "Auth token set");
}
