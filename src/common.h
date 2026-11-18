#ifndef COMMON_H
#define COMMON_H

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

typedef enum {
    MODEL_CORE75 = 0,
    MODEL_COREM1
} DeviceModel;

typedef struct {
    ConnectionMode mode;
    int batteryPercent;       // 0 - 100, or -1 if offline/disconnected
    int lastKnownPercent;     // Retained last known valid percentage even when asleep/offline
    BOOL isCharging;          // TRUE if plugged into USB and charging
    BOOL isSleeping;          // TRUE if dongle present but peripheral asleep/offline
    WCHAR modeName[32];
    int dpiValue;             // e.g. 800, 1600, 3200 (or 0 if unknown)
    int pollingRateHz;        // e.g. 1000, 500, 125 (or 0 if unknown)
} DeviceStatus;

#define MAX_CACHED_TARGET_DEVICES 4
#define MAX_CACHED_NON_TARGET_DEVICES 32

typedef struct {
    HANDLE targets[MAX_CACHED_TARGET_DEVICES];
    int targetCount;
    HANDLE nonTargets[MAX_CACHED_NON_TARGET_DEVICES];
    int nonTargetCount;
    int nonTargetEvictIdx;
} DeviceHandleCache;

typedef struct {
    int lastKnownBattery;
    int pendingOutlierPercent;
    int consecutiveRebounds;
    BOOL freshBaselineNeeded;
    BOOL forceDirectReading;
    int prev2P4GBatt;
    BOOL inferred2P4GCharging;
    int flatChargeCycles;
} DeviceFilterState;

#ifdef __cplusplus
}
#endif

#endif // COMMON_H
