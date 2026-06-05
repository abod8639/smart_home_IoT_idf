#ifndef MDNS_MANAGER_H
#define MDNS_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize mDNS service advertisement.
 *        Makes the device discoverable as "smarthome.local" on the LAN.
 *        Also advertises MQTT and HTTP services for client auto-discovery.
 */
void mdns_manager_init(void);

#ifdef __cplusplus
}
#endif

#endif // MDNS_MANAGER_H
