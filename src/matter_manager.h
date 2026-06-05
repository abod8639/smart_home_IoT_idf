#ifndef MATTER_MANAGER_H
#define MATTER_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize esp-matter endpoints and start the Matter thread
void matter_manager_init(void);

#ifdef __cplusplus
}
#endif

#endif // MATTER_MANAGER_H
