#ifndef MATTER_MANAGER_H
#define MATTER_MANAGER_H

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize esp-matter endpoints and start the Matter thread
void matter_manager_init(void);

// Add a new dynamic endpoint and persist it to NVS.
// device_type: 1 = on_off_light, 2 = on_off_plugin_unit, 3 = dimmable_light
// Returns the Matter endpoint_id (>= 0) on success, or -1 on failure.
int matter_manager_add_endpoint(int device_type, int pin);

// Get the Setup Payload QR Code and Manual Pairing Code strings.
// Call ONLY after MATTER_READY_BIT has been set (i.e., after matter_manager_init()).
void matter_manager_get_setup_payload(char *qr_buf, size_t qr_size, char *manual_buf, size_t manual_size);

#ifdef __cplusplus
}
#endif

#endif // MATTER_MANAGER_H
