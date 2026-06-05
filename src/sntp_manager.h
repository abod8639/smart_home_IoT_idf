#ifndef SNTP_MANAGER_H
#define SNTP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SNTP time synchronization.
 *        Must be called after WiFi is initialized (waits internally for connection).
 *        Uses pool.ntp.org as the time server.
 */
void sntp_manager_init(void);

#ifdef __cplusplus
}
#endif

#endif // SNTP_MANAGER_H
