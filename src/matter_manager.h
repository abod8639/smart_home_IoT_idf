#ifndef MATTER_MANAGER_H
#define MATTER_MANAGER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize esp-matter endpoints and start the Matter thread
void matter_manager_init(void);

// Add a new dynamic endpoint
// device_type: 1 = on_off_light, 2 = on_off_plugin_unit, 3 = dimmable_light
int matter_manager_add_endpoint(int device_type, int pin);

// Get the Setup Payload QR Code and Manual Pairing Code strings
void matter_manager_get_setup_payload(char *qr_buf, size_t qr_size, char *manual_buf, size_t manual_size);

#ifdef __cplusplus
}
#endif

#endif // MATTER_MANAGER_H
