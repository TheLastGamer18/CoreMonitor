#ifndef HID_COMMON_H
#define HID_COMMON_H

#include <windows.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initializes dynamic loading of hid.dll, setupapi.dll, CancelIoEx
BOOL HidCommon_Init(void);

// Releases dynamically loaded libraries
void HidCommon_Cleanup(void);

// Non-blocking zero-access handle check (verifies USB bus presence without handle locking)
BOOL HidCommon_TestDevicePresence(const char *devicePath);

// Unified Device Handle Caching & Raw Input Target Filtering
void HidCommon_ClearHandleCache(DeviceHandleCache *cache);
BOOL HidCommon_IsTargetDevice(
    DeviceHandleCache *cache,
    HANDLE hDevice,
    const char *vidLower,
    const char **pidListLower,
    int pidCount
);

// Unified Battery Outlier Filtering & Registry Persistence
void HidCommon_InitFilter(DeviceFilterState *st, const WCHAR *regKey, const WCHAR *legacyKey);
void HidCommon_ResetFilter(DeviceFilterState *st);
int HidCommon_FilterBatteryReading(
    DeviceFilterState *st,
    int rawPercent,
    BOOL isCharging,
    const WCHAR *regKey,
    const WCHAR *legacyKey
);
BOOL HidCommon_UpdateChargeTrend(
    DeviceFilterState *st,
    int effBatt,
    BOOL liveChargingIndicator,
    BOOL wiredConnected
);

#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
// Performs strictly read-only, non-blocking overlapped VIA Raw HID query
int HidCommon_QueryViaBattery(const char *devicePath, BOOL *outIsCharging);
#endif

#if defined(BUILD_TARGET_COREM1) || (!defined(BUILD_TARGET_CORE75))
// Performs Core M1 (3151 family) battery query via Rongyuan protocol:
// - 2.4G Receiver (0x5045): Feature Report 0xF7 (returns battery, sleep, charging)
// - Wired Mouse (0x5043): Feature Report 0x8A / 0xF7 (returns battery, charging)
int HidCommon_QueryCoreM1Battery(
    const char *devicePath,
    BOOL isWired,
    BOOL *outIsCharging,
    BOOL *outIsSleeping,
    int *outDpi,
    int *outPollingRate
);
#endif

// Scans HID devices matching VID, target PID, and optional usage page/usage.
// When targetUsagePage is 0, skips system-locked pointer collections and selects vendor collections.
BOOL HidCommon_ScanDevice(
    USHORT targetVid,
    USHORT targetPid,
    USHORT targetUsagePage,
    USHORT targetUsage,
    char *outPath,
    DWORD pathMax
);

// Scans PnP Bluetooth devices case-insensitively for friendly names containing filter1 (and optional filter2),
// and extracts the native PKEY_Bluetooth_Battery property. Returns battery % or -1.
int HidCommon_ScanBluetoothBattery(const WCHAR *filter1, const WCHAR *filter2);

#ifdef __cplusplus
}
#endif

#endif // HID_COMMON_H
