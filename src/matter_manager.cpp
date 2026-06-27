#include "matter_manager.h"
extern "C" {
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "mqtt_manager.h"
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

static void init_endpoint_mapping() {
    for (int i = 0; i < MAX_ENDPOINTS; i++) {
        s_endpoint_to_pin[i] = -1;
    }
}

static esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data) {
    if (type == PRE_UPDATE) {
        if (cluster_id == chip::app::Clusters::OnOff::Id && attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
            bool state = val->val.b;
            int pin = (endpoint_id < MAX_ENDPOINTS) ? s_endpoint_to_pin[endpoint_id] : -1;
            
            if (pin >= 0) {
                gpio_set_relay_state(pin, state);
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"event\": \"relay_update\", \"endpoint\": %d, \"state\": %d}", endpoint_id, state);
                mqtt_manager_publish_event(buf);
            }
        }
        else if (cluster_id == chip::app::Clusters::LevelControl::Id && attribute_id == chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id) {
            uint8_t brightness = val->val.u8;
            int pin = (endpoint_id < MAX_ENDPOINTS) ? s_endpoint_to_pin[endpoint_id] : -1;
            
            if (pin >= 0) {
                pwm_set_duty(pin, brightness);
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"event\": \"pwm_update\", \"endpoint\": %d, \"level\": %d}", endpoint_id, brightness);
                mqtt_manager_publish_event(buf);
            }
        }
    }
    return ESP_OK;
}

void matter_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Matter Manager");
    init_endpoint_mapping();
    
    node::config_t node_config;
    s_node = node::create(&node_config, app_attribute_update_cb, NULL);

    // Initial endpoints if needed can be created here
    esp_matter::start(s_node);
}

int matter_manager_add_endpoint(int device_type, int pin) {
    if (!s_node) return -1;
    
    endpoint_t *ep = NULL;
    if (device_type == 1) { // on_off_light
        on_off_light::config_t config;
        ep = on_off_light::create(s_node, &config, ENDPOINT_FLAG_NONE, NULL);
    } else if (device_type == 2) { // on_off_plugin_unit
        on_off_plugin_unit::config_t config;
        ep = on_off_plugin_unit::create(s_node, &config, ENDPOINT_FLAG_NONE, NULL);
    } else if (device_type == 3) { // dimmable_light
        dimmable_light::config_t config;
        ep = dimmable_light::create(s_node, &config, ENDPOINT_FLAG_NONE, NULL);
    } else {
        ESP_LOGW(TAG, "Unsupported Matter device type: %d", device_type);
        return -1;
    }
    
    if (ep) {
        uint16_t endpoint_id = endpoint::get_id(ep);
        if (endpoint_id < MAX_ENDPOINTS) {
            s_endpoint_to_pin[endpoint_id] = pin;
        }
        ESP_LOGI(TAG, "Created Matter endpoint %d for pin %d (type %d)", endpoint_id, pin, device_type);
        
        // Notify matter stack about new endpoint if required dynamically
        esp_matter::endpoint::enable(ep);
        return endpoint_id;
    }
    
    return -1;
}

void matter_manager_get_setup_payload(char *qr_buf, size_t qr_size, char *manual_buf, size_t manual_size) {
    if (qr_buf) qr_buf[0] = '\0';
    if (manual_buf) manual_buf[0] = '\0';
    
    chip::SetupPayload payload;
    uint32_t setupPinCode = 0;
    uint16_t discriminator = 0;
    
    // Fallback default values
    payload.setUpPINCode = 20202021;
    payload.discriminator.SetLongValue(3840);
    payload.version = 0;
    payload.vendorID = 0xFFF1;
    payload.productID = 0x8000;
    payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlag::kBLE);

    if (chip::DeviceLayer::ConfigurationMgr().GetSetupPinCode(setupPinCode) == CHIP_NO_ERROR) {
        payload.setUpPINCode = setupPinCode;
    }
    if (chip::DeviceLayer::ConfigurationMgr().GetSetupDiscriminator(discriminator) == CHIP_NO_ERROR) {
        payload.discriminator.SetLongValue(discriminator);
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

// Dummy implementation for pure ESP-IDF without esp-matter component setup
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MATTER_MANAGER";
void matter_manager_init(void) {
    ESP_LOGW(TAG, "esp-matter component not found. Matter support disabled.");
}

int matter_manager_add_endpoint(int device_type, int pin) {
    ESP_LOGW(TAG, "Matter disabled. Cannot add endpoint for pin %d", pin);
    return -1;
}

void matter_manager_get_setup_payload(char *qr_buf, size_t qr_size, char *manual_buf, size_t manual_size) {
    if (qr_buf && qr_size > 0) {
        snprintf(qr_buf, qr_size, "MT:DummyQRCodePayloadForTesting");
    }
    if (manual_buf && manual_size > 0) {
        snprintf(manual_buf, manual_size, "12345678901");
    }
}

#endif
