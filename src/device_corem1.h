#ifndef DEVICE_COREM1_H
#define DEVICE_COREM1_H

#include <windows.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sleep timeout in minutes for URX Core M1 mouse (1 minute as configured)
#define COREM1_DEFAULT_SLEEP_MINUTES 1

// Initialize CoreM1 subsystem
void DeviceCoreM1_Init(void);

// Invalidate cached device paths / handles (e.g. on PnP device change or suspend)
void DeviceCoreM1_InvalidateCache(void);

// Poll URX Core M1 status across Wired -> 2.4G -> Bluetooth
// allowWirelessQuery: if FALSE, suppresses active 2.4GHz RF queries to allow mouse to sleep
BOOL DeviceCoreM1_Poll(DeviceStatus *outStatus, BOOL allowWirelessQuery);

// Returns TRUE if an ADC transient/outlier is currently being stabilized
BOOL DeviceCoreM1_IsFilteringTransient(void);

// Forces the next poll to directly accept the live reading without rebound/outlier filtering
void DeviceCoreM1_ForceDirectNextPoll(void);

// Resets session baseline so the next query establishes a fresh ground-truth reading
void DeviceCoreM1_ResetBaseline(void);

// Raw Input handling: returns TRUE if input originated from physical URX Core M1 mouse
// outWakeTriggered is set to TRUE if the mouse was asleep and should wake up
BOOL DeviceCoreM1_HandleRawInput(HRAWINPUT hRaw, BOOL isSleeping, BOOL *outWakeTriggered);

// Direct check if a raw input device handle matches cached CoreM1 handles
BOOL DeviceCoreM1_IsTargetDevice(HANDLE hDevice);

// Cleanup
void DeviceCoreM1_Cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_COREM1_H
