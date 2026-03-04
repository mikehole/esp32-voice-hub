/**
 * Conversation History Manager
 * Stores conversation on SD card for context continuity
 */

#include "conversation.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <vector>

// SD Card pins (from Waveshare reference)
#define SDMMC_CMD_PIN   (gpio_num_t)3
#define SDMMC_D0_PIN    (gpio_num_t)5
#define SDMMC_D1_PIN    (gpio_num_t)6
#define SDMMC_D2_PIN    (gpio_num_t)42
#define SDMMC_D3_PIN    (gpio_num_t)2
#define SDMMC_CLK_PIN   (gpio_num_t)4

#define MOUNT_POINT "/sdcard"
#define CONVERSATION_FILE "/sdcard/conversation.txt"

// Message structure
struct Message {
    String role;
    String content;
};

// Conversation storage
static std::vector<Message> messages;
static sdmmc_card_t* card = NULL;
static char sd_info[128] = "SD card not initialized";
static bool sd_mounted = false;

// Escape JSON string
static String escape_json(const String& s) {
    String result;
    result.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

bool conversation_init() {
    Serial.println("Conversation: Initializing SD card...");
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0 = SDMMC_D0_PIN;
    slot_config.d1 = SDMMC_D1_PIN;
    slot_config.d2 = SDMMC_D2_PIN;
    slot_config.d3 = SDMMC_D3_PIN;
    
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            Serial.println("Conversation: Failed to mount SD card filesystem");
        } else {
            Serial.printf("Conversation: Failed to init SD card: %s\n", esp_err_to_name(ret));
        }
        snprintf(sd_info, sizeof(sd_info), "SD card error: %s", esp_err_to_name(ret));
        return false;
    }
    
    sd_mounted = true;
    
    // Get card info
    float capacity_gb = (float)(card->csd.capacity) / 2048 / 1024;
    snprintf(sd_info, sizeof(sd_info), "SD: %.2f GB, %s", capacity_gb, card->cid.name);
    Serial.printf("Conversation: %s\n", sd_info);
    
    // Load existing conversation
    FILE* f = fopen(CONVERSATION_FILE, "r");
    if (f != NULL) {
        char line[2048];
        while (fgets(line, sizeof(line), f) != NULL) {
            // Format: role|content
            char* sep = strchr(line, '|');
            if (sep != NULL) {
                *sep = '\0';
                String role = line;
                String content = sep + 1;
                content.trim();  // Remove newline
                
                Message msg;
                msg.role = role;
                msg.content = content;
                messages.push_back(msg);
            }
        }
        fclose(f);
        Serial.printf("Conversation: Loaded %d messages from SD\n", messages.size());
    } else {
        Serial.println("Conversation: No existing conversation file, starting fresh");
        
        // Add system message
        Message sys;
        sys.role = "system";
        sys.content = "You are Minerva, a helpful voice assistant running on an ESP32 device. Keep responses concise and conversational since they will be spoken aloud.";
        messages.push_back(sys);
        conversation_save();
    }
    
    return true;
}

bool conversation_sd_available() {
    return sd_mounted;
}

void conversation_add_message(const char* role, const char* content) {
    Message msg;
    msg.role = role;
    msg.content = content;
    messages.push_back(msg);
    
    // Trim old messages if over limit (keep system message)
    while (messages.size() > MAX_CONVERSATION_MESSAGES) {
        // Keep first message if it's system
        if (messages[0].role == "system") {
            messages.erase(messages.begin() + 1);
        } else {
            messages.erase(messages.begin());
        }
    }
    
    Serial.printf("Conversation: Added %s message, total: %d\n", role, messages.size());
    
    // Auto-save after each message
    conversation_save();
}

char* conversation_get_messages_json() {
    // Calculate required size
    size_t total_size = 3;  // "[]" + null
    for (const auto& msg : messages) {
        total_size += 30;  // {"role":"","content":""},
        total_size += msg.role.length();
        total_size += msg.content.length() * 2;  // Account for escaping
    }
    
    char* json = (char*)malloc(total_size);
    if (!json) return NULL;
    
    strcpy(json, "[");
    bool first = true;
    
    for (const auto& msg : messages) {
        if (!first) strcat(json, ",");
        first = false;
        
        strcat(json, "{\"role\":\"");
        strcat(json, msg.role.c_str());
        strcat(json, "\",\"content\":\"");
        strcat(json, escape_json(msg.content).c_str());
        strcat(json, "\"}");
    }
    
    strcat(json, "]");
    return json;
}

int conversation_get_count() {
    return messages.size();
}

void conversation_clear() {
    messages.clear();
    
    // Re-add system message
    Message sys;
    sys.role = "system";
    sys.content = "You are Minerva, a helpful voice assistant running on an ESP32 device. Keep responses concise and conversational since they will be spoken aloud.";
    messages.push_back(sys);
    
    conversation_save();
    Serial.println("Conversation: Cleared");
}

bool conversation_save() {
    if (!sd_mounted) return false;
    
    FILE* f = fopen(CONVERSATION_FILE, "w");
    if (f == NULL) {
        Serial.println("Conversation: Failed to open file for writing");
        return false;
    }
    
    for (const auto& msg : messages) {
        fprintf(f, "%s|%s\n", msg.role.c_str(), msg.content.c_str());
    }
    
    fclose(f);
    Serial.printf("Conversation: Saved %d messages to SD\n", messages.size());
    return true;
}

const char* conversation_get_sd_info() {
    return sd_info;
}
