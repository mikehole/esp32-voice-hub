/**
 * Web Server - HTTP endpoints for control and OTA
 */

#include "web_server.h"
#include "ota_update.h"
#include "wifi_manager.h"
#include "audio.h"
#include "display.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "lvgl.h"

// MIN macro if not defined
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "web_srv";
static httpd_handle_t server = NULL;
static char auth_token[64] = {0};

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

// GET /api/status - Device status
static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", "esp32-voice-hub");
    cJSON_AddStringToObject(root, "version", ota_get_version());
    cJSON_AddStringToObject(root, "sha", ota_get_sha256_short());
    cJSON_AddStringToObject(root, "ip", wifi_manager_get_ip());
    cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(root, "framework", "esp-idf");
    
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    
    free(json);
    cJSON_Delete(root);
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
    
    // Read and write in chunks
    char buf[1024];
    int remaining = req->content_len;
    int received;
    
    while (remaining > 0) {
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            ESP_LOGE(TAG, "Receive failed");
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
    
    // Register endpoints
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler
    };
    httpd_register_uri_handler(server, &status_uri);
    
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
    
    ESP_LOGI(TAG, "Server started");
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
