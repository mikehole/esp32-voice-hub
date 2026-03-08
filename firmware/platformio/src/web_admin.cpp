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
#include "conversation.h"
#include "notification.h"
#include "avatar.h"
#include "status_ring.h"

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
"<h3>OpenAI (Whisper)</h3>"
"<div id=\"openai\">Loading...</div>"
"<input type=\"password\" id=\"apikey\" placeholder=\"sk-...\" style=\"width:100%;padding:8px;margin:5px 0;border-radius:5px;border:1px solid #2E86AB;background:#0A1929;color:#5DADE2\">"
"<button onclick=\"saveKey()\">Save API Key</button>"
"<button onclick=\"testKey()\">Test Transcription</button>"
"<div id=\"testresult\" style=\"margin-top:10px\"></div>"
"</div>"
"<div class=\"section\">"
"<h3>OpenClaw</h3>"
"<div id=\"openclaw\">Loading...</div>"
"<input type=\"text\" id=\"ocurl\" placeholder=\"https://mikesdocker\" style=\"width:100%;padding:8px;margin:5px 0;border-radius:5px;border:1px solid #2E86AB;background:#0A1929;color:#5DADE2\">"
"<button onclick=\"saveOC()\">Save Endpoint</button>"
"<br>"
"<input type=\"password\" id=\"octoken\" placeholder=\"Gateway token (OPENCLAW_GATEWAY_TOKEN)\" style=\"width:100%;padding:8px;margin:5px 0;border-radius:5px;border:1px solid #2E86AB;background:#0A1929;color:#5DADE2\">"
"<button onclick=\"saveOCToken()\">Save Token</button>"
"<button onclick=\"testOC()\">Test AI Response</button>"
"<div id=\"ocresult\" style=\"margin-top:10px\"></div>"
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
"document.getElementById('openclaw').innerHTML='Endpoint: '+(d.openclawUrl||'(not set)')+' | Token: '+(d.openclawToken||'(not set)');"
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
"fetch('/api/openai/key',{method:'POST',headers:{'Content-Type':'text/plain'},body:k}).then(r=>r.text()).then(t=>{"
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
"function saveOC(){"
"var u=document.getElementById('ocurl').value;"
"if(!u){alert('Enter OpenClaw URL');return;}"
"fetch('/api/openclaw/endpoint',{method:'POST',headers:{'Content-Type':'text/plain'},body:u}).then(r=>r.text()).then(t=>{"
"alert(t);load();"
"document.getElementById('ocurl').value='';"
"});"
"}"
"function saveOCToken(){"
"var t=document.getElementById('octoken').value;"
"if(!t){alert('Enter hooks token');return;}"
"fetch('/api/openclaw/token',{method:'POST',headers:{'Content-Type':'text/plain'},body:t}).then(r=>r.text()).then(t=>{"
"alert(t);load();"
"document.getElementById('octoken').value='';"
"});"
"}"
"function testOC(){"
"document.getElementById('ocresult').innerText='Transcribing + sending to AI...';"
"fetch('/api/openclaw/ask').then(r=>r.text()).then(t=>{"
"document.getElementById('ocresult').innerText=t;"
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
        "{\"ip\":\"%s\",\"uptime\":\"%s\",\"freeHeap\":\"%u KB\",\"freePsram\":\"%u KB\",\"rssi\":%d,\"brightness\":%d,\"openaiKey\":\"%s\",\"openclawUrl\":\"%s\",\"openclawToken\":\"%s\",\"wakewordHost\":\"%s\",\"wakewordPort\":%d,\"sdCard\":\"%s\",\"conversationCount\":%d}",
        wifi_manager_get_ip().c_str(),
        uptime,
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
        rssi,
        brightness,
        openai_get_api_key(),
        openclaw_get_endpoint(),
        openclaw_get_token(),
        wakeword_get_host(),
        wakeword_get_port(),
        conversation_get_sd_info(),
        conversation_get_count()
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

// OpenAI API key save (POST body)
static esp_err_t openai_key_handler(httpd_req_t *req) {
    char key[256] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(key)) {
        Serial.printf("OpenAI key: invalid content_len=%d\n", content_len);
        httpd_resp_send(req, "Invalid key length", 18);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, key, content_len);
    if (received != content_len) {
        Serial.printf("OpenAI key: recv failed, got %d of %d\n", received, content_len);
        httpd_resp_send(req, "Failed to receive key", 21);
        return ESP_OK;
    }
    key[received] = '\0';
    
    Serial.printf("OpenAI key received: len=%d, key='%.20s...'\n", received, key);
    
    // Basic validation
    if (strncmp(key, "sk-", 3) != 0) {
        httpd_resp_send(req, "Invalid key format (must start with sk-)", 40);
        return ESP_OK;
    }
    
    openai_set_api_key(key);
    char resp[64];
    snprintf(resp, sizeof(resp), "API Key saved: %s", openai_get_api_key());
    httpd_resp_send(req, resp, strlen(resp));
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

// OpenClaw endpoint save (POST body)
static esp_err_t openclaw_endpoint_handler(httpd_req_t *req) {
    char url[128] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(url)) {
        httpd_resp_send(req, "Invalid URL length", 18);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, url, content_len);
    if (received != content_len) {
        httpd_resp_send(req, "Failed to receive URL", 21);
        return ESP_OK;
    }
    url[received] = '\0';
    
    openclaw_set_endpoint(url);
    char resp[160];
    snprintf(resp, sizeof(resp), "Endpoint saved: %s", openclaw_get_endpoint());
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// OpenClaw token save (POST body)
static esp_err_t openclaw_token_handler(httpd_req_t *req) {
    char token[128] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(token)) {
        httpd_resp_send(req, "Invalid token length", 20);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, token, content_len);
    if (received != content_len) {
        httpd_resp_send(req, "Failed to receive token", 23);
        return ESP_OK;
    }
    token[received] = '\0';
    
    openclaw_set_token(token);
    char resp[64];
    snprintf(resp, sizeof(resp), "Token saved: %s", openclaw_get_token());
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// TTS download endpoint - get raw PCM from OpenAI for debugging
static esp_err_t tts_download_handler(httpd_req_t *req) {
    char text[1024] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(text)) {
        httpd_resp_send(req, "Text too long (max 1023 chars)", 30);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, text, content_len);
    if (received != content_len) {
        httpd_resp_send(req, "Failed to receive text", 22);
        return ESP_OK;
    }
    text[received] = '\0';
    
    Serial.printf("TTS Download: '%s'\n", text);
    
    // Get TTS audio
    size_t audio_size = 0;
    uint8_t* audio_data = openai_tts(text, &audio_size);
    
    if (!audio_data) {
        char error[300];
        snprintf(error, sizeof(error), "TTS error: %s", openai_get_last_error());
        httpd_resp_send(req, error, strlen(error));
        return ESP_OK;
    }
    
    // Send raw PCM back
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"tts.pcm\"");
    httpd_resp_send(req, (const char*)audio_data, audio_size);
    
    heap_caps_free(audio_data);
    return ESP_OK;
}

// Chat endpoint - send to OpenClaw with history, then speak response
static esp_err_t chat_handler(httpd_req_t *req) {
    char text[1024] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(text)) {
        httpd_resp_send(req, "Text too long (max 1023 chars)", 30);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, text, content_len);
    if (received != content_len) {
        httpd_resp_send(req, "Failed to receive text", 22);
        return ESP_OK;
    }
    text[received] = '\0';
    
    Serial.printf("Chat: '%s'\n", text);
    
    // Send to OpenClaw with conversation history
    char* response = openclaw_send_with_history(text);
    
    if (!response) {
        char error[300];
        snprintf(error, sizeof(error), "OpenClaw error: %s", openai_get_last_error());
        httpd_resp_send(req, error, strlen(error));
        return ESP_OK;
    }
    
    Serial.printf("Chat response: '%s'\n", response);
    
    // Convert response to speech
    size_t audio_size = 0;
    uint8_t* audio_data = openai_tts(response, &audio_size);
    
    if (!audio_data) {
        // Return text response even if TTS fails
        httpd_resp_send(req, response, strlen(response));
        free(response);
        return ESP_OK;
    }
    
    // Play audio
    audio_play(audio_data, audio_size, 24000);
    heap_caps_free(audio_data);
    
    // Return text response
    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

// Notify endpoint - Queue announcement, show notification avatar, wait for tap
// POST body = text to announce when user acknowledges
// Query params: silent=1 (no attention chime)
static esp_err_t notify_handler(httpd_req_t *req) {
    // Parse query params
    char query[64] = {0};
    bool silent = false;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(query, "silent", param, sizeof(param)) == ESP_OK) {
            silent = (param[0] == '1' || param[0] == 't');
        }
    }
    
    char text[1024] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(text)) {
        httpd_resp_send(req, "Text too long (max 1023 chars)", 30);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, text, content_len);
    if (received != content_len) {
        httpd_resp_send(req, "Failed to receive text", 22);
        return ESP_OK;
    }
    text[received] = '\0';
    
    Serial.printf("Notify: '%s' (silent=%d)\n", text, silent);
    
    // Queue the notification
    if (notification_queue_ex(text, silent)) {
        // Show notification avatar and ring
        avatar_set_state(STATE_NOTIFICATION);
        status_ring_show(STATE_NOTIFICATION);
        
        const char* resp = silent ? "Silent notification queued - tap to hear" 
                                  : "Notification queued - tap to hear";
        httpd_resp_send(req, resp, strlen(resp));
    } else {
        httpd_resp_send(req, "Failed to queue notification", 28);
    }
    
    return ESP_OK;
}

// Notify with audio - Queue pre-loaded audio, show notification avatar, wait for tap
// POST body = raw PCM audio data (mono 16-bit signed)
// Query params: rate=SAMPLERATE (default 24000), text=DisplayText (optional), silent=1 (no chime)
static esp_err_t notify_audio_handler(httpd_req_t *req) {
    // Parse query params
    char query[256] = {0};
    uint32_t sample_rate = 24000;
    char display_text[256] = {0};
    bool silent = false;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[64];
        if (httpd_query_key_value(query, "rate", param, sizeof(param)) == ESP_OK) {
            sample_rate = atoi(param);
        }
        if (httpd_query_key_value(query, "text", display_text, sizeof(display_text)) != ESP_OK) {
            display_text[0] = '\0';
        }
        if (httpd_query_key_value(query, "silent", param, sizeof(param)) == ESP_OK) {
            silent = (param[0] == '1' || param[0] == 't');
        }
    }
    
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "No audio data", 13);
        return ESP_OK;
    }
    
    if (content_len > NOTIFICATION_MAX_AUDIO_SIZE) {
        char err[64];
        snprintf(err, sizeof(err), "Audio too large (max %d bytes)", NOTIFICATION_MAX_AUDIO_SIZE);
        httpd_resp_send(req, err, strlen(err));
        return ESP_OK;
    }
    
    Serial.printf("NotifyAudio: Receiving %d bytes @ %u Hz\n", content_len, sample_rate);
    
    // Allocate buffer in PSRAM for receiving
    uint8_t* audio_data = (uint8_t*)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    if (!audio_data) {
        httpd_resp_send(req, "Failed to allocate buffer", 25);
        return ESP_OK;
    }
    
    // Receive audio data
    int total = 0;
    while (total < content_len) {
        int received = httpd_req_recv(req, (char*)(audio_data + total), content_len - total);
        if (received <= 0) {
            heap_caps_free(audio_data);
            httpd_resp_send(req, "Failed to receive data", 22);
            return ESP_OK;
        }
        total += received;
    }
    
    // Queue the notification with audio
    if (notification_queue_audio_ex(audio_data, content_len, sample_rate, 
                                     display_text[0] ? display_text : NULL, silent)) {
        // Show notification avatar and ring
        avatar_set_state(STATE_NOTIFICATION);
        status_ring_show(STATE_NOTIFICATION);
        
        heap_caps_free(audio_data);  // notification_queue_audio made a copy
        
        char resp[64];
        snprintf(resp, sizeof(resp), "Audio notification queued (%d bytes)", content_len);
        httpd_resp_send(req, resp, strlen(resp));
    } else {
        heap_caps_free(audio_data);
        httpd_resp_send(req, "Failed to queue notification", 28);
    }
    
    return ESP_OK;
}

// Speak endpoint - TTS and play audio (no AI, just speaks text directly)
static esp_err_t speak_handler(httpd_req_t *req) {
    char text[1024] = {0};
    
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)sizeof(text)) {
        httpd_resp_send(req, "Text too long (max 1023 chars)", 30);
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, text, content_len);
    if (received != content_len) {
        httpd_resp_send(req, "Failed to receive text", 22);
        return ESP_OK;
    }
    text[received] = '\0';
    
    Serial.printf("Speak: '%s'\n", text);
    
    // Get TTS audio
    size_t audio_size = 0;
    uint8_t* audio_data = openai_tts(text, &audio_size);
    
    if (!audio_data) {
        char error[300];
        snprintf(error, sizeof(error), "TTS error: %s", openai_get_last_error());
        httpd_resp_send(req, error, strlen(error));
        return ESP_OK;
    }
    
    // Play audio (24kHz PCM from OpenAI)
    bool played = audio_play(audio_data, audio_size, 24000);
    heap_caps_free(audio_data);
    
    if (played) {
        char resp[64];
        snprintf(resp, sizeof(resp), "Spoke %u bytes of audio", audio_size);
        httpd_resp_send(req, resp, strlen(resp));
    } else {
        httpd_resp_send(req, "Playback failed", 15);
    }
    
    return ESP_OK;
}

// OpenClaw ask - transcribe + send to AI
static esp_err_t openclaw_ask_handler(httpd_req_t *req) {
    if (!openai_has_api_key()) {
        httpd_resp_send(req, "Error: No OpenAI API key configured", 35);
        return ESP_OK;
    }
    
    if (!openclaw_has_endpoint()) {
        httpd_resp_send(req, "Error: No OpenClaw endpoint configured", 38);
        return ESP_OK;
    }
    
    // Get last recording
    size_t audio_size = 0;
    const uint8_t* audio_data = audio_get_last_recording(&audio_size);
    
    if (!audio_data || audio_size == 0) {
        httpd_resp_send(req, "Error: No recording available", 29);
        return ESP_OK;
    }
    
    // Transcribe
    char* transcript = openai_transcribe(audio_data, audio_size);
    if (!transcript) {
        char error[300];
        snprintf(error, sizeof(error), "Transcription error: %s", openai_get_last_error());
        httpd_resp_send(req, error, strlen(error));
        return ESP_OK;
    }
    
    // Send to OpenClaw
    char* response = openclaw_send_message(transcript);
    free(transcript);
    
    if (response) {
        httpd_resp_send(req, response, strlen(response));
        free(response);
    } else {
        char error[300];
        snprintf(error, sizeof(error), "OpenClaw error: %s", openai_get_last_error());
        httpd_resp_send(req, error, strlen(error));
    }
    return ESP_OK;
}

// Play raw audio endpoint - POST raw PCM/WAV data
// Query params: rate=SAMPLERATE (default 44100), stereo=1/0 (default 1), wav=1/0 (default 0)
static esp_err_t play_handler(httpd_req_t *req) {
    // Parse query params
    char query[64] = {0};
    uint32_t sample_rate = 44100;
    bool is_stereo = true;
    bool is_wav = false;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "rate", param, sizeof(param)) == ESP_OK) {
            sample_rate = atoi(param);
        }
        if (httpd_query_key_value(query, "stereo", param, sizeof(param)) == ESP_OK) {
            is_stereo = (param[0] == '1');
        }
        if (httpd_query_key_value(query, "wav", param, sizeof(param)) == ESP_OK) {
            is_wav = (param[0] == '1');
        }
    }
    
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "No audio data", 13);
        return ESP_OK;
    }
    
    Serial.printf("Play: Receiving %d bytes, rate=%u, stereo=%d, wav=%d\n", 
                  content_len, sample_rate, is_stereo, is_wav);
    
    // Allocate buffer in PSRAM
    uint8_t* audio_data = (uint8_t*)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    if (!audio_data) {
        httpd_resp_send(req, "Failed to allocate buffer", 25);
        return ESP_OK;
    }
    
    // Receive audio data
    int total = 0;
    while (total < content_len) {
        int received = httpd_req_recv(req, (char*)(audio_data + total), content_len - total);
        if (received <= 0) {
            heap_caps_free(audio_data);
            httpd_resp_send(req, "Failed to receive data", 22);
            return ESP_OK;
        }
        total += received;
    }
    
    // If WAV, skip 44-byte header and extract sample rate
    uint8_t* pcm_data = audio_data;
    size_t pcm_size = content_len;
    if (is_wav && content_len > 44) {
        // Parse WAV header for sample rate
        uint32_t wav_rate = audio_data[24] | (audio_data[25] << 8) | 
                           (audio_data[26] << 16) | (audio_data[27] << 24);
        uint16_t wav_channels = audio_data[22] | (audio_data[23] << 8);
        sample_rate = wav_rate;
        is_stereo = (wav_channels == 2);
        pcm_data = audio_data + 44;
        pcm_size = content_len - 44;
        Serial.printf("Play: WAV header: %u Hz, %d channels\n", wav_rate, wav_channels);
    }
    
    // Show speaking state while playing (for voice hook responses)
    avatar_set_state(STATE_SPEAKING);
    status_ring_show(STATE_SPEAKING);
    
    // Play - if mono, we need to convert to stereo
    bool played;
    if (is_stereo) {
        // Direct stereo playback
        played = audio_play_stereo(pcm_data, pcm_size, sample_rate);
    } else {
        // Mono - use existing mono-to-stereo conversion
        played = audio_play(pcm_data, pcm_size, sample_rate);
    }
    
    // Wait for playback to complete before returning to idle
    while (audio_is_playing()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Return to idle state
    status_ring_hide();
    avatar_set_state(STATE_IDLE);
    
    heap_caps_free(audio_data);
    
    if (played) {
        char resp[64];
        snprintf(resp, sizeof(resp), "Played %u bytes at %u Hz", pcm_size, sample_rate);
        httpd_resp_send(req, resp, strlen(resp));
    } else {
        httpd_resp_send(req, "Playback failed", 15);
    }
    return ESP_OK;
}

// Clear conversation history
static esp_err_t clear_conversation_handler(httpd_req_t *req) {
    conversation_clear();
    httpd_resp_send(req, "Conversation cleared", 20);
    return ESP_OK;
}

// ============ NEW MINERVA CONTROL ENDPOINTS ============

// POST /api/avatar - Upload custom 130x130 RGB565 image (33800 bytes)
static esp_err_t avatar_upload_handler(httpd_req_t *req) {
    size_t content_len = req->content_len;
    const size_t expected_size = 130 * 130 * 2;  // 33800 bytes
    
    if (content_len != expected_size) {
        char err[100];
        snprintf(err, sizeof(err), "Wrong size: got %u, expected %u (130x130 RGB565)", content_len, expected_size);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, err, strlen(err));
        return ESP_OK;
    }
    
    // Allocate buffer
    uint8_t* img_data = (uint8_t*)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    if (!img_data) {
        httpd_resp_send(req, "Out of memory", 13);
        return ESP_OK;
    }
    
    // Receive data
    int total = 0;
    while (total < content_len) {
        int received = httpd_req_recv(req, (char*)(img_data + total), content_len - total);
        if (received <= 0) {
            heap_caps_free(img_data);
            httpd_resp_send(req, "Failed to receive data", 22);
            return ESP_OK;
        }
        total += received;
    }
    
    // Set avatar
    bool success = avatar_set_custom((const uint16_t*)img_data, content_len);
    heap_caps_free(img_data);
    
    if (success) {
        httpd_resp_send(req, "Avatar updated", 14);
    } else {
        httpd_resp_send(req, "Failed to set avatar", 20);
    }
    return ESP_OK;
}

// GET /api/avatar/reset - Reset to normal avatar
static esp_err_t avatar_reset_handler(httpd_req_t *req) {
    avatar_reset_custom();
    httpd_resp_send(req, "Avatar reset", 12);
    return ESP_OK;
}

// POST /api/ring - Show ring with state (?state=thinking|speaking|recording|notification|connecting)
static esp_err_t ring_show_handler(httpd_req_t *req) {
    char query[64] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    
    char state_str[32] = {0};
    httpd_query_key_value(query, "state", state_str, sizeof(state_str));
    
    ProcessingState state = STATE_IDLE;
    if (strcmp(state_str, "connecting") == 0) state = STATE_CONNECTING;
    else if (strcmp(state_str, "recording") == 0) state = STATE_RECORDING;
    else if (strcmp(state_str, "thinking") == 0) state = STATE_THINKING;
    else if (strcmp(state_str, "speaking") == 0) state = STATE_SPEAKING;
    else if (strcmp(state_str, "notification") == 0) state = STATE_NOTIFICATION;
    else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Valid states: connecting, recording, thinking, speaking, notification", 70);
        return ESP_OK;
    }
    
    status_ring_show(state);
    
    char resp[64];
    snprintf(resp, sizeof(resp), "Ring showing: %s", state_str);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// GET /api/ring/hide - Hide ring
static esp_err_t ring_hide_handler(httpd_req_t *req) {
    status_ring_hide();
    httpd_resp_send(req, "Ring hidden", 11);
    return ESP_OK;
}

// GET /api/heap - Memory stats
static esp_err_t heap_handler(httpd_req_t *req) {
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"heap_free\":%u,"
        "\"heap_largest\":%u,"
        "\"heap_min_free\":%u,"
        "\"psram_free\":%u,"
        "\"psram_largest\":%u,"
        "\"psram_total\":%u"
        "}",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM)
    );
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// POST /api/avatar/state - Set avatar pose (?state=idle|thinking|speaking|recording|notification|connecting)
static esp_err_t avatar_state_handler(httpd_req_t *req) {
    char query[64] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    
    char state_str[32] = {0};
    httpd_query_key_value(query, "state", state_str, sizeof(state_str));
    
    ProcessingState state = STATE_IDLE;
    if (strcmp(state_str, "idle") == 0) state = STATE_IDLE;
    else if (strcmp(state_str, "connecting") == 0) state = STATE_CONNECTING;
    else if (strcmp(state_str, "recording") == 0) state = STATE_RECORDING;
    else if (strcmp(state_str, "thinking") == 0) state = STATE_THINKING;
    else if (strcmp(state_str, "speaking") == 0) state = STATE_SPEAKING;
    else if (strcmp(state_str, "notification") == 0) state = STATE_NOTIFICATION;
    else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Valid states: idle, connecting, recording, thinking, speaking, notification", 76);
        return ESP_OK;
    }
    
    avatar_set_state(state);
    
    char resp[64];
    snprintf(resp, sizeof(resp), "Avatar state: %s", state_str);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// GET /api/volume - Get volume, POST /api/volume?v=N - Set volume (0-100)
static esp_err_t volume_handler(httpd_req_t *req) {
    char query[32] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    
    char vol_str[8] = {0};
    if (httpd_query_key_value(query, "v", vol_str, sizeof(vol_str)) == ESP_OK) {
        int vol = atoi(vol_str);
        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;
        audio_set_volume(vol);
    }
    
    char resp[32];
    snprintf(resp, sizeof(resp), "{\"volume\":%d}", audio_get_volume());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

void web_admin_register(httpd_handle_t server) {
    httpd_uri_t admin = { .uri = "/admin", .method = HTTP_GET, .handler = admin_handler };
    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler };
    httpd_uri_t clear_conv = { .uri = "/api/conversation/clear", .method = HTTP_GET, .handler = clear_conversation_handler };
    httpd_uri_t brightness = { .uri = "/api/brightness", .method = HTTP_GET, .handler = brightness_handler };
    httpd_uri_t clear = { .uri = "/api/clear-wifi", .method = HTTP_GET, .handler = clear_wifi_handler };
    httpd_uri_t screenshot = { .uri = "/api/screenshot", .method = HTTP_GET, .handler = screenshot_handler };
    httpd_uri_t audio_start = { .uri = "/api/audio/start", .method = HTTP_GET, .handler = audio_start_handler };
    httpd_uri_t audio_stop = { .uri = "/api/audio/stop", .method = HTTP_GET, .handler = audio_stop_handler };
    httpd_uri_t audio_download = { .uri = "/api/audio/download", .method = HTTP_GET, .handler = audio_download_handler };
    httpd_uri_t openai_key = { .uri = "/api/openai/key", .method = HTTP_POST, .handler = openai_key_handler };
    httpd_uri_t openai_transcribe = { .uri = "/api/openai/transcribe", .method = HTTP_GET, .handler = openai_transcribe_handler };
    httpd_uri_t oc_endpoint = { .uri = "/api/openclaw/endpoint", .method = HTTP_POST, .handler = openclaw_endpoint_handler };
    httpd_uri_t oc_token = { .uri = "/api/openclaw/token", .method = HTTP_POST, .handler = openclaw_token_handler };
    httpd_uri_t oc_ask = { .uri = "/api/openclaw/ask", .method = HTTP_GET, .handler = openclaw_ask_handler };
    httpd_uri_t chat = { .uri = "/api/chat", .method = HTTP_POST, .handler = chat_handler };
    httpd_uri_t speak = { .uri = "/api/speak", .method = HTTP_POST, .handler = speak_handler };
    httpd_uri_t notify = { .uri = "/api/notify", .method = HTTP_POST, .handler = notify_handler };
    httpd_uri_t notify_audio = { .uri = "/api/notify-audio", .method = HTTP_POST, .handler = notify_audio_handler };
    httpd_uri_t tts_download = { .uri = "/api/tts", .method = HTTP_POST, .handler = tts_download_handler };
    httpd_uri_t play = { .uri = "/api/play", .method = HTTP_POST, .handler = play_handler };
    
    // New Minerva control endpoints
    httpd_uri_t avatar_upload = { .uri = "/api/avatar", .method = HTTP_POST, .handler = avatar_upload_handler };
    httpd_uri_t avatar_reset = { .uri = "/api/avatar/reset", .method = HTTP_GET, .handler = avatar_reset_handler };
    httpd_uri_t avatar_state = { .uri = "/api/avatar/state", .method = HTTP_GET, .handler = avatar_state_handler };
    httpd_uri_t ring_show = { .uri = "/api/ring", .method = HTTP_GET, .handler = ring_show_handler };
    httpd_uri_t ring_hide = { .uri = "/api/ring/hide", .method = HTTP_GET, .handler = ring_hide_handler };
    httpd_uri_t heap_info = { .uri = "/api/heap", .method = HTTP_GET, .handler = heap_handler };
    httpd_uri_t volume = { .uri = "/api/volume", .method = HTTP_GET, .handler = volume_handler };
    
    esp_err_t err;
    err = httpd_register_uri_handler(server, &admin);
    Serial.printf("Admin: /admin registered: %d\n", err);
    err = httpd_register_uri_handler(server, &status);
    Serial.printf("Admin: /api/status registered: %d\n", err);
    err = httpd_register_uri_handler(server, &brightness);
    err = httpd_register_uri_handler(server, &clear);
    err = httpd_register_uri_handler(server, &clear_conv);
    err = httpd_register_uri_handler(server, &screenshot);
    err = httpd_register_uri_handler(server, &audio_start);
    err = httpd_register_uri_handler(server, &audio_stop);
    err = httpd_register_uri_handler(server, &audio_download);
    err = httpd_register_uri_handler(server, &openai_key);
    err = httpd_register_uri_handler(server, &openai_transcribe);
    err = httpd_register_uri_handler(server, &oc_endpoint);
    err = httpd_register_uri_handler(server, &oc_token);
    err = httpd_register_uri_handler(server, &oc_ask);
    err = httpd_register_uri_handler(server, &chat);
    err = httpd_register_uri_handler(server, &speak);
    err = httpd_register_uri_handler(server, &notify);
    err = httpd_register_uri_handler(server, &notify_audio);
    err = httpd_register_uri_handler(server, &tts_download);
    err = httpd_register_uri_handler(server, &play);
    
    // Register new Minerva control endpoints
    err = httpd_register_uri_handler(server, &avatar_upload);
    err = httpd_register_uri_handler(server, &avatar_reset);
    err = httpd_register_uri_handler(server, &avatar_state);
    err = httpd_register_uri_handler(server, &ring_show);
    err = httpd_register_uri_handler(server, &ring_hide);
    err = httpd_register_uri_handler(server, &heap_info);
    err = httpd_register_uri_handler(server, &volume);
    Serial.printf("Admin: Minerva control endpoints registered: %d\n", err);
    
    Serial.println("Admin endpoints registered");
}
