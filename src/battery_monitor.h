#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODE_DISCONNECTED = 0,
    MODE_WIRED,
    MODE_2P4G,
    MODE_BLUETOOTH
} ConnectionMode;

typedef struct {
    ConnectionMode mode;
    int batteryPercent;       // 0 - 100, or -1 if offline/disconnected
    int lastKnownPercent;     // Retained last known valid percentage even when asleep/offline
    BOOL isCharging;          // TRUE if plugged into USB and charging
    BOOL isSleeping;          // TRUE if dongle present but keyboard asleep/offline
    int sleepTimerMinutes;    // Detected sleep timer in minutes (1, 3, 10, 30; default 3)
    WCHAR statusText[128];
    WCHAR modeName[32];
    WCHAR stateName[32];      // "Active", "Asleep", "Charging", "Fully Charged", "Offline"
} URXStatus;

// Initialize hardware monitoring subsystems
BOOL BatteryMonitor_Init(void);

// Invalidate cached device handles/paths (e.g., when DBT_DEVICEARRIVAL or DBT_DEVICEREMOVECOMPLETE occurs)
void BatteryMonitor_InvalidateCache(void);

// Query URX Core75 status across Wired -> 2.4G -> Bluetooth
// allowWirelessQuery: if FALSE, suppresses active 2.4GHz RF queries to allow keyboard to sleep
void BatteryMonitor_Poll(URXStatus *outStatus, BOOL allowWirelessQuery);

// Get the detected sleep timer in minutes (default 3 if unqueried/offline)
int BatteryMonitor_GetSleepTimerMinutes(void);

// Reset transient failure counters (e.g., when keystrokes confirm keyboard is awake)
void BatteryMonitor_ResetFailures(void);

// Returns TRUE if an ADC transient/outlier is currently being stabilized
BOOL BatteryMonitor_IsFilteringTransient(void);

// Cleanup any open handles or resources
void BatteryMonitor_Cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_MONITOR_H
