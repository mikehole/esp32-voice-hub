/**
 * Update Checker - Check GitHub releases for firmware updates
 */

#include "update_checker.h"
#include "ota_update.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "update";

// GitHub API URL for releases (use /releases not /releases/latest for pre-releases)
#define GITHUB_API_URL "https://api.github.com/repos/mikehole/esp32-voice-hub/releases/tags/latest"
#define FIRMWARE_ASSET_NAME "esp32_voice_hub.bin"

// State
static bool update_available = false;
static char latest_version[32] = {0};
static char download_url[256] = {0};
static char response_buffer[12288] = {0};  // 12KB for GitHub API response
static int response_len = 0;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (response_len + evt->data_len < sizeof(response_buffer) - 1) {
                memcpy(response_buffer + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
                response_buffer[response_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

void update_checker_init(void)
{
    ESP_LOGI(TAG, "Update checker initialized");
}

void update_checker_check(void (*callback)(bool update_available, const char* new_version))
{
    ESP_LOGI(TAG, "Checking for updates...");
    
    response_len = 0;
    response_buffer[0] = '\0';
    update_available = false;
    
    esp_http_client_config_t config = {
        .url = GITHUB_API_URL,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // GitHub API requires User-Agent header
    esp_http_client_set_header(client, "User-Agent", "ESP32-Voice-Hub");
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "GitHub API status: %d, response: %d bytes", status, response_len);
        
        if (status == 200 && response_len > 0) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(response_buffer);
            if (json) {
                cJSON *tag = cJSON_GetObjectItem(json, "tag_name");
                cJSON *assets = cJSON_GetObjectItem(json, "assets");
                cJSON *body = cJSON_GetObjectItem(json, "body");
                cJSON *name_obj = cJSON_GetObjectItem(json, "name");
                
                // Try to extract SHA from release name "Latest Build (abc1234)"
                // or from body "**Commit:** abc1234"
                char remote_sha[16] = {0};
                
                if (name_obj && cJSON_IsString(name_obj)) {
                    // Parse "Latest Build (abc1234)" -> extract abc1234
                    const char* name_str = name_obj->valuestring;
                    const char* paren = strchr(name_str, '(');
                    if (paren) {
                        paren++;  // Skip '('
                        int i = 0;
                        while (*paren && *paren != ')' && i < 15) {
                            remote_sha[i++] = *paren++;
                        }
                        remote_sha[i] = '\0';
                    }
                }
                
                // Fallback: try to parse from body
                if (strlen(remote_sha) == 0 && body && cJSON_IsString(body)) {
                    const char* commit_marker = "**Commit:** ";
                    const char* pos = strstr(body->valuestring, commit_marker);
                    if (pos) {
                        pos += strlen(commit_marker);
                        int i = 0;
                        while (*pos && *pos != '\n' && *pos != ' ' && i < 15) {
                            remote_sha[i++] = *pos++;
                        }
                        remote_sha[i] = '\0';
                    }
                }
                
                if (tag && cJSON_IsString(tag)) {
                    strncpy(latest_version, tag->valuestring, sizeof(latest_version) - 1);
                }
                
                // Get current device SHA
                const char* current_sha = ota_get_sha256_short();
                
                // Log what we found
                ESP_LOGI(TAG, "Release: %s, remote SHA: %s, local SHA: %s",
                    latest_version, 
                    strlen(remote_sha) > 0 ? remote_sha : "(unknown)",
                    current_sha);
                
                // Compare SHAs to determine if update is available
                if (strlen(remote_sha) > 0 && strlen(current_sha) > 0) {
                    // We have both SHAs - compare them
                    update_available = (strcmp(remote_sha, current_sha) != 0);
                    if (update_available) {
                        strncpy(latest_version, remote_sha, sizeof(latest_version) - 1);
                    }
                } else if (assets && cJSON_GetArraySize(assets) > 0) {
                    // Fallback: no SHA comparison possible, assume update if assets exist
                    // This shouldn't happen once CI includes SHA
                    ESP_LOGW(TAG, "No SHA found in release, assuming update available");
                    update_available = true;
                }
                
                ESP_LOGI(TAG, "Update available: %s", update_available ? "yes" : "no");
                
                // Find firmware asset URL
                if (assets && cJSON_IsArray(assets)) {
                    cJSON *asset;
                    cJSON_ArrayForEach(asset, assets) {
                        cJSON *name = cJSON_GetObjectItem(asset, "name");
                        cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
                        
                        if (name && cJSON_IsString(name) && 
                            strcmp(name->valuestring, FIRMWARE_ASSET_NAME) == 0 &&
                            url && cJSON_IsString(url)) {
                            strncpy(download_url, url->valuestring, sizeof(download_url) - 1);
                            ESP_LOGI(TAG, "Firmware URL: %s", download_url);
                            break;
                        }
                    }
                }
                
                cJSON_Delete(json);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    
    if (callback) {
        callback(update_available, latest_version);
    }
}

const char* update_checker_get_url(void)
{
    return download_url;
}

bool update_checker_install(void)
{
    if (strlen(download_url) == 0) {
        ESP_LOGE(TAG, "No download URL available");
        return false;
    }
    
    ESP_LOGI(TAG, "Installing update from: %s", download_url);
    return ota_update_from_url(download_url);
}

bool update_checker_has_update(void)
{
    return update_available;
}

const char* update_checker_get_latest_version(void)
{
    return latest_version;
}
