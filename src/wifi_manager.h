#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * @brief Global WiFi event group.
 *        Consumers wait on WIFI_CONNECTED_BIT before using the network.
 */
extern EventGroupHandle_t g_wifi_event_group;

/** Set when the station has a valid IP address. Cleared on disconnect. */
#define WIFI_CONNECTED_BIT  BIT0

/** Set once esp_matter::start() has fully completed and the stack is ready. */
#define MATTER_READY_BIT    BIT1

/**
 * @brief Initialize the WiFi station and start connecting.
 *        Credentials are read from sdkconfig (menuconfig).
 */
void wifi_manager_init(void);

/**
 * @brief Return the current AP RSSI or -100 if not connected.
 */
int wifi_manager_get_rssi(void);

/**
 * @brief Start the Access Point and Captive Portal for Wi-Fi provisioning.
 */
void wifi_manager_start_captive_portal(void);

#endif // WIFI_MANAGER_H
