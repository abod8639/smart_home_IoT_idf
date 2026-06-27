#include "matter_manager.h"
extern "C" {
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "mqtt_manager.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
}
#include "esp_log.h"
#include <stdio.h>

#if __has_include("esp_matter.h")
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <app/server/Server.h>
#include <setup_payload/SetupPayload.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static const char *TAG = "MATTER_MANAGER";
static node_t *s_node = NULL;

// Store mapping of endpoint ID to physical pin
#define MAX_ENDPOINTS 32
static int s_endpoint_to_pin[MAX_ENDPOINTS];

// Track how many dynamic endpoints have been created this boot
// (used to pick the right NVS slot when saving a new device)
static int s_dynamic_endpoint_count = 0;

static void init_endpoint_mapping() {
    for (int i = 0; i < MAX_ENDPOINTS; i++) {
        s_endpoint_to_pin[i] = -1;
    }
}

// ---------------------------------------------------------------------------
// Attribute update callback — called by the Matter stack on attribute writes
// ---------------------------------------------------------------------------
static esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data) {
    if (type == PRE_UPDATE) {
        if (cluster_id == chip::app::Clusters::OnOff::Id &&
            attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
            bool state = val->val.b;
            int pin = (endpoint_id < MAX_ENDPOINTS) ? s_endpoint_to_pin[endpoint_id] : -1;

            if (pin >= 0) {
                gpio_set_relay_state(pin, state);
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "{\"event\": \"relay_update\", \"endpoint\": %d, \"state\": %d}",
                         endpoint_id, state);
                mqtt_manager_publish_event(buf);
            }
        } else if (cluster_id == chip::app::Clusters::LevelControl::Id &&
                   attribute_id == chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id) {
            uint8_t brightness = val->val.u8;
            int pin = (endpoint_id < MAX_ENDPOINTS) ? s_endpoint_to_pin[endpoint_id] : -1;

            if (pin >= 0) {
                pwm_set_duty(pin, brightness);
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "{\"event\": \"pwm_update\", \"endpoint\": %d, \"level\": %d}",
                         endpoint_id, brightness);
                mqtt_manager_publish_event(buf);
            }
        }
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Internal: create one endpoint from type+pin (shared by init and add)
// Returns the Matter endpoint_id, or -1 on failure.
// Does NOT persist to NVS — persistence is the caller's responsibility.
// ---------------------------------------------------------------------------
static int _create_endpoint(int device_type, int pin_num) {
    if (!s_node) return -1;

    endpoint_t *ep = NULL;
    if (device_type == 1) {
        on_off_light::config_t cfg;
        ep = on_off_light::create(s_node, &cfg, ENDPOINT_FLAG_NONE, NULL);
    } else if (device_type == 2) {
        on_off_plugin_unit::config_t cfg;
        ep = on_off_plugin_unit::create(s_node, &cfg, ENDPOINT_FLAG_NONE, NULL);
    } else if (device_type == 3) {
        dimmable_light::config_t cfg;
        ep = dimmable_light::create(s_node, &cfg, ENDPOINT_FLAG_NONE, NULL);
    } else {
        ESP_LOGW(TAG, "Unsupported Matter device type: %d", device_type);
        return -1;
    }

    if (!ep) {
        ESP_LOGE(TAG, "Failed to create endpoint for type=%d pin=%d", device_type, pin_num);
        return -1;
    }

    uint16_t endpoint_id = endpoint::get_id(ep);
    if (endpoint_id < MAX_ENDPOINTS) {
        s_endpoint_to_pin[endpoint_id] = pin_num;
    }
    ESP_LOGI(TAG, "Created Matter endpoint %d → pin %d (type %d)", endpoint_id, pin_num, device_type);
    return (int)endpoint_id;
}

// ---------------------------------------------------------------------------
// Internal: restore all endpoints saved in NVS BEFORE esp_matter::start()
// ---------------------------------------------------------------------------
static void _load_saved_endpoints_from_nvs(void) {
    int count = nvs_get_matter_device_count();
    if (count == 0) {
        ESP_LOGI(TAG, "No persisted Matter endpoints found in NVS");
        return;
    }

    ESP_LOGI(TAG, "Restoring %d Matter endpoint(s) from NVS...", count);
    for (int i = 0; i < count && i < NVS_MAX_MATTER_DEVICES; i++) {
        int device_type = 0, pin_num = 0;
        if (nvs_get_matter_device(i, &device_type, &pin_num)) {
            int ep_id = _create_endpoint(device_type, pin_num);
            if (ep_id >= 0) {
                s_dynamic_endpoint_count++;
                ESP_LOGI(TAG, "  [NVS slot %d] restored endpoint %d (type=%d pin=%d)",
                         i, ep_id, device_type, pin_num);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public: init — build node, restore saved endpoints, then start Matter
// ---------------------------------------------------------------------------
void matter_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Matter Manager");
    init_endpoint_mapping();

    node::config_t node_config;
    s_node = node::create(&node_config, app_attribute_update_cb, NULL);

    // ── KEY FIX 1 ──────────────────────────────────────────────────────────
    // Restore persisted endpoints BEFORE esp_matter::start() so that the
    // Matter Commissioner discovers them all in a single commissioning pass.
    // ───────────────────────────────────────────────────────────────────────
    _load_saved_endpoints_from_nvs();

    esp_matter::start(s_node);

    // ── KEY FIX 2 ──────────────────────────────────────────────────────────
    // Give the Matter stack ~500 ms to fully initialise before signalling
    // firebase_poll_task to read the real Setup Payload (QR code).
    // Without this delay, GetSetupPinCode() may return an error and the
    // task falls back to the dummy PIN 20202021.
    // ───────────────────────────────────────────────────────────────────────
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupSetBits(g_wifi_event_group, MATTER_READY_BIT);
    ESP_LOGI(TAG, "Matter stack ready — MATTER_READY_BIT set");
}

// ---------------------------------------------------------------------------
// Public: add a new dynamic endpoint at runtime + persist it
// ---------------------------------------------------------------------------
int matter_manager_add_endpoint(int device_type, int pin) {
    if (!s_node) return -1;

    int endpoint_id = _create_endpoint(device_type, pin);
    if (endpoint_id < 0) return -1;

    // Enable the endpoint in the running stack
    endpoint_t *ep = endpoint::get(s_node, (uint16_t)endpoint_id);
    if (ep) {
        esp_matter::endpoint::enable(ep);
    }

    // ── KEY FIX 3 ──────────────────────────────────────────────────────────
    // Persist this new endpoint to NVS so it survives a reboot and is
    // recreated BEFORE esp_matter::start() on the next boot.
    // ───────────────────────────────────────────────────────────────────────
    int slot = nvs_get_matter_device_count();
    if (slot < NVS_MAX_MATTER_DEVICES) {
        nvs_save_matter_device(slot, device_type, pin);
        nvs_increment_matter_device_count();
        s_dynamic_endpoint_count++;
        ESP_LOGI(TAG, "Matter endpoint %d persisted to NVS slot %d", endpoint_id, slot);
    } else {
        ESP_LOGW(TAG, "NVS Matter device slots full (%d max). Endpoint %d NOT persisted.",
                 NVS_MAX_MATTER_DEVICES, endpoint_id);
    }

    return endpoint_id;
}

// ---------------------------------------------------------------------------
// Public: get QR code and manual code strings
// ---------------------------------------------------------------------------
void matter_manager_get_setup_payload(char *qr_buf, size_t qr_size, char *manual_buf, size_t manual_size) {
    if (qr_buf)     qr_buf[0]     = '\0';
    if (manual_buf) manual_buf[0] = '\0';

    chip::SetupPayload payload;
    uint32_t setupPinCode  = 0;
    uint16_t discriminator = 0;

    // Sensible defaults — overwritten below if the stack has real values
    payload.setUpPINCode = 20202021;
    payload.discriminator.SetLongValue(3840);
    payload.version = 0;
    payload.vendorID  = 0xFFF1;
    payload.productID = 0x8000;
    payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlag::kBLE);

    if (chip::DeviceLayer::ConfigurationMgr().GetSetupPinCode(setupPinCode) == CHIP_NO_ERROR) {
        payload.setUpPINCode = setupPinCode;
    } else {
        ESP_LOGW(TAG, "GetSetupPinCode failed — using default 20202021");
    }

    if (chip::DeviceLayer::ConfigurationMgr().GetSetupDiscriminator(discriminator) == CHIP_NO_ERROR) {
        payload.discriminator.SetLongValue(discriminator);
    } else {
        ESP_LOGW(TAG, "GetSetupDiscriminator failed — using default 3840");
    }

    if (qr_buf && qr_size > 0) {
        std::string qrString;
        chip::QRCodeSetupPayloadGenerator(payload).payloadBase38Representation(qrString);
        snprintf(qr_buf, qr_size, "%s", qrString.c_str());
    }

    if (manual_buf && manual_size > 0) {
        std::string manualString;
        chip::ManualSetupPayloadGenerator(payload).payloadDecimalStringRepresentation(manualString);
        snprintf(manual_buf, manual_size, "%s", manualString.c_str());
    }
}

#else

// ---------------------------------------------------------------------------
// Stub implementation — built when esp-matter component is not present
// ---------------------------------------------------------------------------
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MATTER_MANAGER";

void matter_manager_init(void) {
    ESP_LOGW(TAG, "esp-matter component not found. Matter support disabled.");

    // Still set MATTER_READY_BIT so firebase_poll_task is not blocked forever.
    extern EventGroupHandle_t g_wifi_event_group;
    xEventGroupSetBits(g_wifi_event_group, MATTER_READY_BIT);
}

int matter_manager_add_endpoint(int device_type, int pin) {
    ESP_LOGW(TAG, "Matter disabled. Cannot add endpoint for pin %d", pin);
    return -1;
}

void matter_manager_get_setup_payload(char *qr_buf, size_t qr_size, char *manual_buf, size_t manual_size) {
    if (qr_buf     && qr_size     > 0) snprintf(qr_buf,     qr_size,     "MT:DummyQRCodePayloadForTesting");
    if (manual_buf && manual_size > 0) snprintf(manual_buf, manual_size, "12345678901");
}

#endif
