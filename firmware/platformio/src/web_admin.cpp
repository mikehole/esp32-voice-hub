/**
 * Web Admin Interface
 * Status, settings, and screenshot endpoints
 */

#include "web_admin.h"
#include <Arduino.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include "lvgl.h"
#include "audio_capture.h"
#include "openai_client.h"

// Brightness callbacks
static brightness_getter_t get_brightness = NULL;
static brightness_setter_t set_brightness = NULL;

void web_admin_set_brightness_callbacks(brightness_getter_t getter, brightness_setter_t setter) {
    get_brightness = getter;
    set_brightness = setter;
}

// Admin page HTML
static const char ADMIN_HTML[] PROGMEM = 
"<!DOCTYPE html><html><head>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Minerva Admin</title>"
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
"input[type=range]{width:100%}"
".section{margin-top:20px;padding-top:15px;border-top:2px solid #2E86AB}"
"#screen{max-width:100%;border-radius:10px;margin-top:10px}"
"</style></head><body>"
"<div class=\"c\">"
"<div class=\"l\">&#129417;</div>"
"<h1>Minerva Admin</h1>"
"<div id=\"status\">Loading...</div>"
"<div class=\"section\">"
"<h3>Display</h3>"
"<label>Brightness: <span id=\"bval\">100</span>%</label>"
"<input type=\"range\" id=\"brightness\" min=\"10\" max=\"100\" value=\"100\" onchange=\"setBrightness(this.value)\">"
"<br><button onclick=\"screenshot()\">Take Screenshot</button>"
"<div id=\"screenwrap\"></div>"
"</div>"
"<div class=\"section\">"
"<h3>Audio</h3>"
"<div id=\"audio\">No recording</div>"
"<button onclick=\"startRec()\">Start Recording</button>"
"<button onclick=\"stopRec()\">Stop Recording</button>"
"<button onclick=\"downloadAudio()\">Download WAV</button>"
"</div>"
"<div class=\"section\">"
"<h3>OpenAI</h3>"
"<div id=\"openai\">Loading...</div>"
"<input type=\"password\" id=\"apikey\" placeholder=\"sk-...\" style=\"width:100%;padding:8px;margin:5px 0;border-radius:5px;border:1px solid #2E86AB;background:#0A1929;color:#5DADE2\">"
"<button onclick=\"saveKey()\">Save API Key</button>"
"<button onclick=\"testKey()\">Test Transcription</button>"
"<div id=\"testresult\"></div>"
"</div>"
"<div class=\"section\">"
"<h3>WiFi</h3>"
"<button class=\"danger\" onclick=\"clearWifi()\">Clear WiFi Credentials</button>"
"</div>"
"</div>"
"<script>"
"function load(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"var h='';"
"h+='<div class=\"row\"><span class=\"label\">IP Address</span><span class=\"value\">'+d.ip+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Uptime</span><span class=\"value\">'+d.uptime+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Free Heap</span><span class=\"value\">'+d.freeHeap+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">Free PSRAM</span><span class=\"value\">'+d.freePsram+'</span></div>';"
"h+='<div class=\"row\"><span class=\"label\">WiFi Signal</span><span class=\"value\">'+d.rssi+' dBm</span></div>';"
"document.getElementById('status').innerHTML=h;"
"document.getElementById('brightness').value=d.brightness;"
"document.getElementById('bval').innerText=d.brightness;"
"document.getElementById('openai').innerHTML='API Key: '+(d.openaiKey||'(not set)');"
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
"function clearWifi(){"
"if(confirm('Clear WiFi credentials? Device will restart in AP mode.')){"
"fetch('/api/clear-wifi').then(()=>alert('Credentials cleared. Restarting...'));"
"}"
"}"
"function startRec(){"
"document.getElementById('audio').innerText='Starting...';"
"fetch('/api/audio/start').then(r=>r.text()).then(t=>document.getElementById('audio').innerText=t);"
"}"
"function stopRec(){"
"document.getElementById('audio').innerText='Stopping...';"
"fetch('/api/audio/stop').then(r=>r.text()).then(t=>document.getElementById('audio').innerText=t);"
"}"
"function downloadAudio(){"
"window.location='/api/audio/download';"
"}"
"function saveKey(){"
"var k=document.getElementById('apikey').value;"
"if(!k){alert('Enter API key');return;}"
"fetch('/api/openai/key?k='+encodeURIComponent(k)).then(r=>r.text()).then(t=>{"
"document.getElementById('openai').innerText=t;"
"document.getElementById('apikey').value='';"
"});"
"}"
"function testKey(){"
"document.getElementById('testresult').innerText='Transcribing last recording...';"
"fetch('/api/openai/transcribe').then(r=>r.text()).then(t=>{"
"document.getElementById('testresult').innerText=t;"
"});"
"}"
"load();setInterval(load,5000);"
"</script></body></html>";

// Status endpoint - JSON
static esp_err_t status_handler(httpd_req_t *req) {
    char json[512];
    
    // Get WiFi RSSI
    wifi_ap_record_t ap_info;
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    
    // Format uptime
    unsigned long secs = millis() / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs = mins / 60;
    
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%luh %lum %lus", hrs, mins % 60, secs % 60);
    
    // Get brightness
    int brightness = get_brightness ? get_brightness() : 100;
    
    snprintf(json, sizeof(json),
        "{\"ip\":\"%s\",\"uptime\":\"%s\",\"freeHeap\":\"%u KB\",\"freePsram\":\"%u KB\",\"rssi\":%d,\"brightness\":%d,\"openaiKey\":\"%s\"}",
        wifi_manager_get_ip().c_str(),
        uptime,
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
        rssi,
        brightness,
        openai_get_api_key()
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Admin page
static esp_err_t admin_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ADMIN_HTML, strlen(ADMIN_HTML));
    return ESP_OK;
}

// Brightness control
static esp_err_t brightness_handler(httpd_req_t *req) {
    char query[64] = {0};
    char val[8] = {0};
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "v", val, sizeof(val));
    }
    
    int brightness = atoi(val);
    if (brightness >= 10 && brightness <= 100 && set_brightness) {
        set_brightness(brightness);
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Clear WiFi credentials
static esp_err_t clear_wifi_handler(httpd_req_t *req) {
    wifi_manager_clear_credentials();
    httpd_resp_send(req, "OK", 2);
    
    // Restart after delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

// Screenshot - capture display as BMP
static esp_err_t screenshot_handler(httpd_req_t *req) {
    // Get screen dimensions
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t w = lv_obj_get_width(scr);
    lv_coord_t h = lv_obj_get_height(scr);
    
    // Take LVGL snapshot
    lv_img_dsc_t *snapshot = lv_snapshot_take(scr, LV_IMG_CF_TRUE_COLOR);
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
            // RGB565 with byte swap -> BGR24
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

// Audio start recording
static esp_err_t audio_start_handler(httpd_req_t *req) {
    if (audio_is_recording()) {
        httpd_resp_send(req, "Already recording", 17);
        return ESP_OK;
    }
    if (audio_start_recording()) {
        httpd_resp_send(req, "Recording started", 17);
    } else {
        httpd_resp_send(req, "Failed to start", 15);
    }
    return ESP_OK;
}

// Audio stop recording
static esp_err_t audio_stop_handler(httpd_req_t *req) {
    if (!audio_is_recording()) {
        size_t size = 0;
        audio_get_last_recording(&size);
        char msg[64];
        snprintf(msg, sizeof(msg), "Not recording. Last: %u bytes", size);
        httpd_resp_send(req, msg, strlen(msg));
        return ESP_OK;
    }
    size_t size = 0;
    audio_stop_recording(&size);
    char msg[64];
    snprintf(msg, sizeof(msg), "Stopped. Recorded %u bytes", size);
    httpd_resp_send(req, msg, strlen(msg));
    return ESP_OK;
}

// Audio download as WAV
static esp_err_t audio_download_handler(httpd_req_t *req) {
    size_t audio_size = 0;
    const uint8_t* audio_data = audio_get_last_recording(&audio_size);
    
    if (!audio_data || audio_size == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No recording available");
        return ESP_FAIL;
    }
    
    // WAV header (44 bytes) + audio data
    uint32_t wav_size = 44 + audio_size;
    uint8_t* wav = (uint8_t*)heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM);
    if (!wav) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    // WAV header
    uint32_t sample_rate = 16000;
    uint16_t bits_per_sample = 16;
    uint16_t channels = 1;
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;
    
    // RIFF header
    memcpy(wav, "RIFF", 4);
    uint32_t chunk_size = wav_size - 8;
    wav[4] = chunk_size & 0xFF;
    wav[5] = (chunk_size >> 8) & 0xFF;
    wav[6] = (chunk_size >> 16) & 0xFF;
    wav[7] = (chunk_size >> 24) & 0xFF;
    memcpy(wav + 8, "WAVE", 4);
    
    // fmt chunk
    memcpy(wav + 12, "fmt ", 4);
    wav[16] = 16; wav[17] = 0; wav[18] = 0; wav[19] = 0;  // Subchunk size
    wav[20] = 1; wav[21] = 0;  // PCM format
    wav[22] = channels; wav[23] = 0;
    wav[24] = sample_rate & 0xFF;
    wav[25] = (sample_rate >> 8) & 0xFF;
    wav[26] = (sample_rate >> 16) & 0xFF;
    wav[27] = (sample_rate >> 24) & 0xFF;
    wav[28] = byte_rate & 0xFF;
    wav[29] = (byte_rate >> 8) & 0xFF;
    wav[30] = (byte_rate >> 16) & 0xFF;
    wav[31] = (byte_rate >> 24) & 0xFF;
    wav[32] = block_align; wav[33] = 0;
    wav[34] = bits_per_sample; wav[35] = 0;
    
    // data chunk
    memcpy(wav + 36, "data", 4);
    wav[40] = audio_size & 0xFF;
    wav[41] = (audio_size >> 8) & 0xFF;
    wav[42] = (audio_size >> 16) & 0xFF;
    wav[43] = (audio_size >> 24) & 0xFF;
    
    // Copy audio data
    memcpy(wav + 44, audio_data, audio_size);
    
    // Send response
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"recording.wav\"");
    httpd_resp_send(req, (char*)wav, wav_size);
    
    heap_caps_free(wav);
    return ESP_OK;
}

// OpenAI API key save
static esp_err_t openai_key_handler(httpd_req_t *req) {
    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char key[128] = {0};
        if (httpd_query_key_value(query, "k", key, sizeof(key)) == ESP_OK) {
            // URL decode the key
            char decoded[128] = {0};
            int j = 0;
            for (int i = 0; key[i] && j < (int)sizeof(decoded) - 1; i++) {
                if (key[i] == '%' && key[i+1] && key[i+2]) {
                    char hex[3] = {key[i+1], key[i+2], 0};
                    decoded[j++] = (char)strtol(hex, NULL, 16);
                    i += 2;
                } else if (key[i] == '+') {
                    decoded[j++] = ' ';
                } else {
                    decoded[j++] = key[i];
                }
            }
            openai_set_api_key(decoded);
            char resp[64];
            snprintf(resp, sizeof(resp), "API Key saved: %s", openai_get_api_key());
            httpd_resp_send(req, resp, strlen(resp));
            return ESP_OK;
        }
    }
    httpd_resp_send(req, "Missing key parameter", 21);
    return ESP_OK;
}

// OpenAI transcription test
static esp_err_t openai_transcribe_handler(httpd_req_t *req) {
    if (!openai_has_api_key()) {
        httpd_resp_send(req, "Error: No API key configured", 28);
        return ESP_OK;
    }
    
    size_t audio_size = 0;
    const uint8_t* audio_data = audio_get_last_recording(&audio_size);
    
    if (!audio_data || audio_size == 0) {
        httpd_resp_send(req, "Error: No recording available", 29);
        return ESP_OK;
    }
    
    char* transcript = openai_transcribe(audio_data, audio_size);
    if (transcript) {
        httpd_resp_send(req, transcript, strlen(transcript));
        free(transcript);
    } else {
        char error[300];
        snprintf(error, sizeof(error), "Error: %s", openai_get_last_error());
        httpd_resp_send(req, error, strlen(error));
    }
    return ESP_OK;
}

void web_admin_register(httpd_handle_t server) {
    httpd_uri_t admin = { .uri = "/admin", .method = HTTP_GET, .handler = admin_handler };
    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler };
    httpd_uri_t brightness = { .uri = "/api/brightness", .method = HTTP_GET, .handler = brightness_handler };
    httpd_uri_t clear = { .uri = "/api/clear-wifi", .method = HTTP_GET, .handler = clear_wifi_handler };
    httpd_uri_t screenshot = { .uri = "/api/screenshot", .method = HTTP_GET, .handler = screenshot_handler };
    httpd_uri_t audio_start = { .uri = "/api/audio/start", .method = HTTP_GET, .handler = audio_start_handler };
    httpd_uri_t audio_stop = { .uri = "/api/audio/stop", .method = HTTP_GET, .handler = audio_stop_handler };
    httpd_uri_t audio_download = { .uri = "/api/audio/download", .method = HTTP_GET, .handler = audio_download_handler };
    httpd_uri_t openai_key = { .uri = "/api/openai/key", .method = HTTP_GET, .handler = openai_key_handler };
    httpd_uri_t openai_transcribe = { .uri = "/api/openai/transcribe", .method = HTTP_GET, .handler = openai_transcribe_handler };
    
    esp_err_t err;
    err = httpd_register_uri_handler(server, &admin);
    Serial.printf("Admin: /admin registered: %d\n", err);
    err = httpd_register_uri_handler(server, &status);
    Serial.printf("Admin: /api/status registered: %d\n", err);
    err = httpd_register_uri_handler(server, &brightness);
    err = httpd_register_uri_handler(server, &clear);
    err = httpd_register_uri_handler(server, &screenshot);
    err = httpd_register_uri_handler(server, &audio_start);
    err = httpd_register_uri_handler(server, &audio_stop);
    err = httpd_register_uri_handler(server, &audio_download);
    err = httpd_register_uri_handler(server, &openai_key);
    err = httpd_register_uri_handler(server, &openai_transcribe);
    Serial.printf("Admin: /api/openai/* registered: %d\n", err);
    
    Serial.println("Admin endpoints registered");
}
