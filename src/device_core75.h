#ifndef DEVICE_CORE75_H
#define DEVICE_CORE75_H

#include <windows.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CORE75_DEFAULT_SLEEP_MINUTES 3

// Initialize Core75 subsystem
void DeviceCore75_Init(void);

// Invalidate cached device paths / handles (e.g. on PnP device change or suspend)
void DeviceCore75_InvalidateCache(void);

// Poll URX Core75 status across Wired -> 2.4G -> Bluetooth
// allowWirelessQuery: if FALSE, suppresses active 2.4GHz RF queries to allow keyboard to sleep
BOOL DeviceCore75_Poll(DeviceStatus *outStatus, BOOL allowWirelessQuery);

// Returns TRUE if an ADC transient/outlier is currently being stabilized
BOOL DeviceCore75_IsFilteringTransient(void);

// Forces the next poll to directly accept the live reading without rebound/outlier filtering
void DeviceCore75_ForceDirectNextPoll(void);

// Resets session baseline so the next query establishes a fresh ground-truth reading
void DeviceCore75_ResetBaseline(void);

// Raw Input handling: returns TRUE if input originated from physical URX Core75 keyboard
// outWakeTriggered: set to TRUE if input woke keyboard from sleep
// outCapsChanged: set to TRUE if Caps Lock state toggled on this input
BOOL DeviceCore75_HandleRawInput(HRAWINPUT hRaw, BOOL *outWakeTriggered, BOOL *outCapsChanged);

// Returns TRUE if Caps Lock is currently toggled ON for Core75
BOOL DeviceCore75_IsCapsLock(void);

// Direct check if a raw input device handle matches cached Core75 handles
BOOL DeviceCore75_IsTargetDevice(HANDLE hDevice);

// Cleanup
void DeviceCore75_Cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_CORE75_H
