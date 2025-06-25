#include "Common.h"

#ifdef __cplusplus
extern "C" {
#endif

void* SystemStateMonitor_create(Modes_t* modes);
void SystemStateMonitor_start(void* obj);
void SystemStateMonitor_stop(void* obj);
void SystemStateMonitor_destroy(void* obj);

#ifdef __cplusplus
}
#endif

