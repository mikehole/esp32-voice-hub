/**
 * @file bluetooth_hid.c
 * @brief Bluetooth HID implementation using NimBLE
 */

#include "bluetooth_hid.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_BT_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
// Removed battery service to save space

static const char* TAG = "BT_HID";

// Device name shown during pairing
#define DEVICE_NAME "VoiceHub"  // Short name to fit in 31-byte adv packet

// HID Service UUID: 0x1812
static const ble_uuid16_t hid_svc_uuid = BLE_UUID16_INIT(0x1812);

// HID Report characteristic UUIDs
static const ble_uuid16_t hid_report_uuid = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t hid_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t hid_info_uuid = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t hid_ctrl_pt_uuid = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t hid_report_ref_uuid = BLE_UUID16_INIT(0x2908);  // Report Reference descriptor
static const ble_uuid16_t hid_protocol_mode_uuid = BLE_UUID16_INIT(0x2A4E);  // Protocol Mode

// Protocol Mode: 0x00 = Boot Protocol, 0x01 = Report Protocol
static uint8_t protocol_mode = 0x01;  // Report Protocol (required for consumer controls)

// Report Reference values: [Report ID, Report Type]
// Report Type: 0x01 = Input, 0x02 = Output, 0x03 = Feature
static const uint8_t keyboard_report_ref[] = { 0x01, 0x01 };  // Report ID 1, Input
static const uint8_t consumer_report_ref[] = { 0x02, 0x01 };  // Report ID 2, Input

// Connection handle
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bt_state_t current_state = BT_STATE_OFF;
static char peer_name[32] = {0};
static uint8_t own_addr_type;

// Attribute handles for notifications
static uint16_t keyboard_report_handle;
static uint16_t consumer_report_handle;

// HID Report Map - describes our HID device capabilities
// Combined keyboard + consumer control device
static const uint8_t hid_report_map[] = {
    // Keyboard Report (Report ID 1)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224) - Left Control
    0x29, 0xE7,        //   Usage Maximum (231) - Right GUI
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) - Modifier byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) - Reserved byte
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array) - Key arrays (6 keys)
    0xC0,              // End Collection

    // Consumer Control Report (Report ID 2)
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0,              // End Collection
};

// HID Information characteristic value
static const uint8_t hid_info[] = {
    0x11, 0x01,  // HID version 1.11
    0x00,        // Country code (not localized)
    0x02,        // Flags: normally connectable
};

// Forward declarations
static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void on_sync(void);
static void on_reset(int reason);
static int hid_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_dsc_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

// Descriptors for keyboard report
static const struct ble_gatt_dsc_def keyboard_report_dscs[] = {
    {
        .uuid = &hid_report_ref_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .access_cb = hid_dsc_access,
        .arg = (void*)keyboard_report_ref,
    },
    { 0 }  // End
};

// Descriptors for consumer report
static const struct ble_gatt_dsc_def consumer_report_dscs[] = {
    {
        .uuid = &hid_report_ref_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .access_cb = hid_dsc_access,
        .arg = (void*)consumer_report_ref,
    },
    { 0 }  // End
};

// GATT service definitions
static const struct ble_gatt_chr_def hid_characteristics[] = {
    {
        // HID Report Map
        .uuid = &hid_report_map_uuid.u,
        .access_cb = hid_chr_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        // HID Information
        .uuid = &hid_info_uuid.u,
        .access_cb = hid_chr_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        // HID Control Point
        .uuid = &hid_ctrl_pt_uuid.u,
        .access_cb = hid_chr_access,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        // Protocol Mode (required for HOGP)
        .uuid = &hid_protocol_mode_uuid.u,
        .access_cb = hid_chr_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        // Keyboard Report (Report ID 1)
        .uuid = &hid_report_uuid.u,
        .access_cb = hid_chr_access,
        .val_handle = &keyboard_report_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
        .descriptors = keyboard_report_dscs,
    },
    {
        // Consumer Report (Report ID 2)
        .uuid = &hid_report_uuid.u,
        .access_cb = hid_chr_access,
        .val_handle = &consumer_report_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
        .descriptors = consumer_report_dscs,
    },
    {
        0,  // End of characteristics
    },
};

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &hid_svc_uuid.u,
        .characteristics = hid_characteristics,
    },
    {
        0,  // End of services
    },
};

// GATT descriptor access callback - returns Report Reference
static int hid_dsc_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const uint8_t *report_ref = (const uint8_t *)arg;
    int rc = os_mbuf_append(ctxt->om, report_ref, 2);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// GATT characteristic access callback
static int hid_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    
    if (ble_uuid_cmp(uuid, &hid_report_map_uuid.u) == 0) {
        // Return HID Report Map
        int rc = os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    
    if (ble_uuid_cmp(uuid, &hid_info_uuid.u) == 0) {
        // Return HID Information
        int rc = os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    
    if (ble_uuid_cmp(uuid, &hid_ctrl_pt_uuid.u) == 0) {
        // HID Control Point write - just acknowledge
        ESP_LOGI(TAG, "HID Control Point write");
        return 0;
    }
    
    if (ble_uuid_cmp(uuid, &hid_protocol_mode_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            int rc = os_mbuf_append(ctxt->om, &protocol_mode, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 1) {
                os_mbuf_copydata(ctxt->om, 0, 1, &protocol_mode);
                ESP_LOGI(TAG, "Protocol mode set to %d", protocol_mode);
            }
            return 0;
        }
    }
    
    if (ble_uuid_cmp(uuid, &hid_report_uuid.u) == 0) {
        // Report read - return empty report
        uint8_t empty_report[8] = {0};
        int rc = os_mbuf_append(ctxt->om, empty_report, sizeof(empty_report));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    
    return BLE_ATT_ERR_UNLIKELY;
}

// GAP event handler
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                conn_handle = event->connect.conn_handle;
                current_state = BT_STATE_CONNECTED;
                ESP_LOGI(TAG, "Connected, handle=%d", conn_handle);
                
                // Try to get peer device name
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(conn_handle, &desc) == 0) {
                    // Could resolve name from address here
                    snprintf(peer_name, sizeof(peer_name), "Device");
                }
                
                // Initiate security/pairing - HID requires encryption
                int rc = ble_gap_security_initiate(conn_handle);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Failed to initiate security: %d", rc);
                } else {
                    ESP_LOGI(TAG, "Security initiated");
                }
            } else {
                ESP_LOGW(TAG, "Connection failed, status=%d", event->connect.status);
                current_state = BT_STATE_ADVERTISING;
                bluetooth_hid_start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected, reason=%d", event->disconnect.reason);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            peer_name[0] = '\0';
            current_state = BT_STATE_ADVERTISING;
            bluetooth_hid_start_advertising();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Advertising complete");
            if (current_state == BT_STATE_ADVERTISING) {
                bluetooth_hid_start_advertising();
            }
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);
            // For now, use "just works" pairing (no passkey)
            if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
                struct ble_sm_io pk;
                pk.action = event->passkey.params.action;
                ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "Encryption change: status=%d", event->enc_change.status);
            break;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // Delete old bond and allow re-pairing
            ESP_LOGI(TAG, "Repeat pairing requested");
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe: attr_handle=%d, cur_notify=%d",
                     event->subscribe.attr_handle, event->subscribe.cur_notify);
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU update: conn_handle=%d, mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;
    }
    return 0;
}

// Called when NimBLE host resets
static void on_reset(int reason) {
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

// Called when NimBLE host syncs with controller
static void on_sync(void) {
    ESP_LOGI(TAG, "BLE host synced");
    
    // Use NRPA (non-resolvable private address) for privacy
    ble_hs_id_infer_auto(0, &own_addr_type);
    
    // Start advertising
    bluetooth_hid_start_advertising();
}

// NimBLE host task
static void nimble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool bluetooth_hid_init(void) {
    ESP_LOGI(TAG, "Initializing Bluetooth HID...");
    
    // Initialize NVS (required for Bluetooth bonding storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE: %d", ret);
        return false;
    }
    
    // Configure NimBLE host
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;  // "Just Works" pairing
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;  // Secure connections
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;  // Store status callback
    
    // Initialize NVS store for bonding
    ble_store_config_init();
    
    // Initialize GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();
    // Battery service removed to save space
    
    // Register our HID service
    int rc = ble_gatts_count_cfg(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return false;
    }
    
    rc = ble_gatts_add_svcs(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return false;
    }
    
    // Set device name
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set device name: %d", rc);
    }
    
    // Set appearance to keyboard
    ble_svc_gap_device_appearance_set(0x03C1);  // Keyboard
    
    // Start NimBLE host task
    nimble_port_freertos_init(nimble_host_task);
    
    current_state = BT_STATE_ADVERTISING;
    ESP_LOGI(TAG, "Bluetooth HID initialized");
    return true;
}

void bluetooth_hid_deinit(void) {
    if (current_state == BT_STATE_OFF) return;
    
    bluetooth_hid_disconnect();
    nimble_port_stop();
    nimble_port_deinit();
    current_state = BT_STATE_OFF;
    ESP_LOGI(TAG, "Bluetooth HID deinitialized");
}

bool bluetooth_hid_start_advertising(void) {
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Already connected, not advertising");
        return false;
    }
    
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    
    // Advertising data - keep minimal to fit 31-byte limit
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
        return false;
    }
    
    // Scan response with HID service UUID
    struct ble_hs_adv_fields rsp_fields = {0};
    ble_uuid16_t uuids16[] = { BLE_UUID16_INIT(0x1812) };  // HID Service
    rsp_fields.uuids16 = uuids16;
    rsp_fields.num_uuids16 = 1;
    rsp_fields.uuids16_is_complete = 1;
    
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response: %d", rc);
        return false;
    }
    
    // Advertising parameters
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // General discoverable
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return false;
    }
    
    current_state = BT_STATE_ADVERTISING;
    ESP_LOGI(TAG, "Started advertising as '%s'", DEVICE_NAME);
    ESP_LOGI(TAG, "Report handles: keyboard=%d, consumer=%d", keyboard_report_handle, consumer_report_handle);
    return true;
}

void bluetooth_hid_stop_advertising(void) {
    ble_gap_adv_stop();
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        current_state = BT_STATE_OFF;
    }
    ESP_LOGI(TAG, "Stopped advertising");
}

void bluetooth_hid_disconnect(void) {
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

bt_state_t bluetooth_hid_get_state(void) {
    return current_state;
}

bool bluetooth_hid_is_connected(void) {
    return conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool bluetooth_hid_get_peer_name(char* buf, size_t len) {
    if (peer_name[0] == '\0') return false;
    strncpy(buf, peer_name, len);
    buf[len - 1] = '\0';
    return true;
}

// Send keyboard report (Report ID 1)
static bool send_keyboard_report(uint8_t modifiers, uint8_t key) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Not connected, cannot send keyboard report");
        return false;
    }
    
    // Keyboard report: [ReportID, Modifiers, Reserved, Key1, Key2, Key3, Key4, Key5, Key6]
    uint8_t report[9] = {
        0x01,       // Report ID
        modifiers,  // Modifier keys
        0x00,       // Reserved
        key,        // Key 1
        0x00, 0x00, 0x00, 0x00, 0x00  // Keys 2-6
    };
    
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for keyboard report");
        return false;
    }
    
    int rc = ble_gatts_notify_custom(conn_handle, keyboard_report_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send keyboard report: %d", rc);
        return false;
    }
    
    return true;
}

// Send consumer control report (Report ID 2)
static bool send_consumer_report(uint16_t key) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Not connected, cannot send consumer report");
        return false;
    }
    
    // Consumer report: [ReportID, Key (16-bit LE)]
    uint8_t report[3] = {
        0x02,               // Report ID
        key & 0xFF,         // Key low byte
        (key >> 8) & 0xFF   // Key high byte
    };
    
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for consumer report");
        return false;
    }
    
    ESP_LOGI(TAG, "Sending consumer report to handle %d, key=0x%04X", consumer_report_handle, key);
    int rc = ble_gatts_notify_custom(conn_handle, consumer_report_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send consumer report: %d (handle=%d)", rc, consumer_report_handle);
        return false;
    }
    
    ESP_LOGI(TAG, "Consumer report sent OK");
    return true;
}

bool bluetooth_hid_send_consumer_key(hid_consumer_key_t key) {
    ESP_LOGI(TAG, "Sending consumer key: 0x%02X", key);
    // Send key down
    if (!send_consumer_report(key)) {
        ESP_LOGE(TAG, "Failed to send key down");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    // Send key up
    if (!send_consumer_report(0)) {
        ESP_LOGE(TAG, "Failed to send key up");
        return false;
    }
    ESP_LOGI(TAG, "Consumer key sent successfully");
    return true;
}

bool bluetooth_hid_send_key(uint8_t modifiers, uint8_t key) {
    return send_keyboard_report(modifiers, key);
}

bool bluetooth_hid_send_shortcut(uint8_t modifiers, uint8_t key) {
    // Press key
    if (!send_keyboard_report(modifiers, key)) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    // Release key
    return send_keyboard_report(0, 0);
}

// Convenience functions
bool bluetooth_hid_volume_up(void) {
    return bluetooth_hid_send_consumer_key(HID_CC_VOLUME_UP);
}

bool bluetooth_hid_volume_down(void) {
    return bluetooth_hid_send_consumer_key(HID_CC_VOLUME_DOWN);
}

bool bluetooth_hid_play_pause(void) {
    return bluetooth_hid_send_consumer_key(HID_CC_PLAY_PAUSE);
}

bool bluetooth_hid_next_track(void) {
    return bluetooth_hid_send_consumer_key(HID_CC_NEXT_TRACK);
}

bool bluetooth_hid_prev_track(void) {
    return bluetooth_hid_send_consumer_key(HID_CC_PREV_TRACK);
}

bool bluetooth_hid_mute(void) {
    return bluetooth_hid_send_consumer_key(HID_CC_MUTE);
}

#else  // CONFIG_BT_ENABLED not set

// Stub implementations when Bluetooth is disabled

bool bluetooth_hid_init(void) {
    ESP_LOGW("BT_HID", "Bluetooth not enabled in sdkconfig");
    return false;
}

void bluetooth_hid_deinit(void) {}
bool bluetooth_hid_start_advertising(void) { return false; }
void bluetooth_hid_stop_advertising(void) {}
void bluetooth_hid_disconnect(void) {}
bt_state_t bluetooth_hid_get_state(void) { return BT_STATE_OFF; }
bool bluetooth_hid_is_connected(void) { return false; }
bool bluetooth_hid_get_peer_name(char* buf, size_t len) { return false; }
bool bluetooth_hid_send_consumer_key(hid_consumer_key_t key) { return false; }
bool bluetooth_hid_send_key(uint8_t modifiers, uint8_t key) { return false; }
bool bluetooth_hid_send_shortcut(uint8_t modifiers, uint8_t key) { return false; }
bool bluetooth_hid_volume_up(void) { return false; }
bool bluetooth_hid_volume_down(void) { return false; }
bool bluetooth_hid_play_pause(void) { return false; }
bool bluetooth_hid_next_track(void) { return false; }
bool bluetooth_hid_prev_track(void) { return false; }
bool bluetooth_hid_mute(void) { return false; }

#endif  // CONFIG_BT_ENABLED
