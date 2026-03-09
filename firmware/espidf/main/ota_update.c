/**
 * OTA Update Module
 * Supports both URL-based and direct upload updates
 */

#include "ota_update.h"
#include <string.h>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_app_format.h"

static const char *TAG = "ota";

// Version info
#define FIRMWARE_VERSION "0.1.0-espidf"
static char sha256_short[9] = {0};

// OTA state for streaming updates
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t* update_partition = NULL;

void ota_init(void)
{
    // Get SHA256 of running firmware
    const esp_app_desc_t* app_desc = esp_app_get_description();
    uint8_t sha256[32];
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha256);
    
    // Convert first 4 bytes to hex string
    for (int i = 0; i < 4; i++) {
        sprintf(&sha256_short[i*2], "%02x", sha256[i]);
    }
    
    ESP_LOGI(TAG, "Running firmware: %s (%.8s)", FIRMWARE_VERSION, sha256_short);
}

const char* ota_get_version(void)
{
    return FIRMWARE_VERSION;
}

const char* ota_get_sha256_short(void)
{
    return sha256_short;
}

esp_err_t ota_update_from_url(const char* url)
{
    ESP_LOGI(TAG, "Starting OTA from URL: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
    };
    
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    
    esp_err_t ret = esp_https_ota(&ota_config);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful, restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t ota_begin(void)
{
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Writing to partition: %s at 0x%lx", 
             update_partition->label, update_partition->address);
    
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "OTA begin successful");
    return ESP_OK;
}

esp_err_t ota_write(const uint8_t* data, size_t len)
{
    if (ota_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = esp_ota_write(ota_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ota_end(void)
{
    if (ota_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_handle = 0;
        return err;
    }
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_handle = 0;
        return err;
    }
    
    ota_handle = 0;
    ESP_LOGI(TAG, "OTA complete! Restarting...");
    esp_restart();
    
    return ESP_OK;  // Never reached
}

esp_err_t ota_abort(void)
{
    if (ota_handle != 0) {
        esp_ota_abort(ota_handle);
        ota_handle = 0;
        ESP_LOGW(TAG, "OTA aborted");
    }
    return ESP_OK;
}
