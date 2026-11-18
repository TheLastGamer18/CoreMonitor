#define _WIN32_WINNT 0x0601
#include "device_corem1.h"
#include "hid_common.h"
#include <stdio.h>

#define COREM1_VID       0x3151
#define COREM1_PID_WIRED 0x5043
#define COREM1_PID_2P4G  0x5045
#define COREM1_PID_BT    0x503C

#define REG_COREM1_KEY L"Software\\CoreMonitor\\Stats\\CoreM1"

static char s_cachedWiredPath[MAX_PATH * 2] = {0};
static char s_cached2P4GPath[MAX_PATH * 2] = {0};

static DeviceFilterState s_filter = {0};
static DeviceHandleCache s_handleCache = {0};
static const char *s_coreM1Pids[] = { "5045", "5043", "503c" };

static DWORD s_lastThrottledInputTick = 0;
static int s_lastKnownDpi = 800;
static int s_lastKnownRateHz = 1000;

// Live packet timing to measure polling rate across all modes
static DWORD s_lastMotionTick = 0;
static int s_motionIntervalSum = 0;
static int s_motionSampleCount = 0;

static void LoadPersistedTelemetry(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_COREM1_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LastKnownDpi", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            if (val >= 50 && val <= 32000) s_lastKnownDpi = (int)val;
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LastKnownRateHz", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            if (val >= 125 && val <= 8000) s_lastKnownRateHz = (int)val;
        }
        RegCloseKey(hKey);
    }
}

static void SavePersistedTelemetry(void) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_COREM1_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = (DWORD)s_lastKnownDpi;
        RegSetValueExW(hKey, L"LastKnownDpi", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        val = (DWORD)s_lastKnownRateHz;
        RegSetValueExW(hKey, L"LastKnownRateHz", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

void DeviceCoreM1_Init(void) {
    HidCommon_InitFilter(&s_filter, REG_COREM1_KEY, NULL);
    HidCommon_ClearHandleCache(&s_handleCache);
    LoadPersistedTelemetry();
    s_lastThrottledInputTick = 0;
    s_lastMotionTick = 0;
    s_motionIntervalSum = 0;
    s_motionSampleCount = 0;
}

void DeviceCoreM1_InvalidateCache(void) {
    s_cachedWiredPath[0] = '\0';
    s_cached2P4GPath[0] = '\0';
    HidCommon_ResetFilter(&s_filter);
    HidCommon_ClearHandleCache(&s_handleCache);
}

BOOL DeviceCoreM1_IsFilteringTransient(void) {
    return (s_filter.pendingOutlierPercent >= 0);
}

void DeviceCoreM1_ForceDirectNextPoll(void) {
    s_filter.forceDirectReading = TRUE;
}

void DeviceCoreM1_ResetBaseline(void) {
    HidCommon_ResetFilter(&s_filter);
}

BOOL DeviceCoreM1_Poll(DeviceStatus *outStatus, BOOL allowWirelessQuery) {
    if (!outStatus) return FALSE;
    memset(outStatus, 0, sizeof(DeviceStatus));
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_filter.lastKnownBattery;
    outStatus->dpiValue = s_lastKnownDpi;
    outStatus->pollingRateHz = s_lastKnownRateHz;

    // 1. Priority 1: Check Wired Mode (VID 0x3151, PID 0x5043)
    int wiredBatt = -1;
    BOOL wiredFound = FALSE;
    int liveDpi = 0;
    int liveRate = 0;

    if (s_cachedWiredPath[0] != '\0') {
        if (HidCommon_TestDevicePresence(s_cachedWiredPath)) {
            wiredFound = TRUE;
            BOOL dummyChg = FALSE, dummySleep = FALSE;
            wiredBatt = HidCommon_QueryCoreM1Battery(s_cachedWiredPath, TRUE, &dummyChg, &dummySleep, &liveDpi, &liveRate);
        } else {
            s_cachedWiredPath[0] = '\0';
        }
    }

    if (!wiredFound) {
        wiredFound = HidCommon_ScanDevice(COREM1_VID, COREM1_PID_WIRED, 0, 0, s_cachedWiredPath, sizeof(s_cachedWiredPath));
        if (wiredFound) {
            BOOL dummyChg = FALSE, dummySleep = FALSE;
            wiredBatt = HidCommon_QueryCoreM1Battery(s_cachedWiredPath, TRUE, &dummyChg, &dummySleep, &liveDpi, &liveRate);
        }
    }

    if (wiredFound && wiredBatt >= 0) {
        if (liveDpi > 0) s_lastKnownDpi = liveDpi;
        if (liveRate > 0) s_lastKnownRateHz = liveRate;
        SavePersistedTelemetry();

        int effBatt = HidCommon_FilterBatteryReading(&s_filter, wiredBatt, TRUE, REG_COREM1_KEY, NULL);
        outStatus->mode = MODE_WIRED;
        outStatus->batteryPercent = effBatt;
        outStatus->lastKnownPercent = effBatt;
        outStatus->isCharging = (effBatt < 100);
        outStatus->isSleeping = FALSE;
        outStatus->dpiValue = s_lastKnownDpi;
        outStatus->pollingRateHz = s_lastKnownRateHz;
        wcscpy(outStatus->modeName, L"Wired USB");
        return TRUE;
    }

    // 2. Priority 2: Check 2.4GHz Dongle Mode (VID 0x3151, PID 0x5045)
    int dongleBatt = -1;
    BOOL dongleFound = FALSE;

    if (s_cached2P4GPath[0] != '\0') {
        if (HidCommon_TestDevicePresence(s_cached2P4GPath)) {
            dongleFound = TRUE;
        } else {
            s_cached2P4GPath[0] = '\0';
        }
    }

    if (!dongleFound) {
        dongleFound = HidCommon_ScanDevice(COREM1_VID, COREM1_PID_2P4G, 0, 0, s_cached2P4GPath, sizeof(s_cached2P4GPath));
    }

    if (dongleFound) {
        outStatus->mode = MODE_2P4G;
        wcscpy(outStatus->modeName, L"2.4GHz Wireless");
        outStatus->isSleeping = FALSE;
        outStatus->dpiValue = s_lastKnownDpi;
        outStatus->pollingRateHz = s_lastKnownRateHz;

        BOOL wirelessCharging = FALSE;
        BOOL wirelessSleeping = FALSE;
        if (allowWirelessQuery && s_cached2P4GPath[0] != '\0') {
            dongleBatt = HidCommon_QueryCoreM1Battery(s_cached2P4GPath, FALSE, &wirelessCharging, &wirelessSleeping, NULL, NULL);
        }

        outStatus->isSleeping = wirelessSleeping;

        int effBatt = s_filter.lastKnownBattery;
        if (dongleBatt > 0) {
            effBatt = HidCommon_FilterBatteryReading(&s_filter, dongleBatt, s_filter.inferred2P4GCharging || wirelessCharging || wiredFound, REG_COREM1_KEY, NULL);
            outStatus->batteryPercent = effBatt;
            outStatus->lastKnownPercent = effBatt;
        } else {
            outStatus->batteryPercent = effBatt;
            outStatus->lastKnownPercent = effBatt;
            s_filter.forceDirectReading = FALSE;
        }

        outStatus->isCharging = HidCommon_UpdateChargeTrend(&s_filter, effBatt, wirelessCharging, wiredFound);
        return (dongleBatt > 0);
    }

    // 3. Priority 3: Check Bluetooth Mode (VID 0x3151, PID 0x503C, Name "URX Core M1")
    int btBatt = HidCommon_ScanBluetoothBattery(L"Core M1", NULL);
    if (btBatt >= 0) {
        int effBatt = HidCommon_FilterBatteryReading(&s_filter, btBatt, FALSE, REG_COREM1_KEY, NULL);
        outStatus->mode = MODE_BLUETOOTH;
        outStatus->batteryPercent = effBatt;
        outStatus->lastKnownPercent = effBatt;
        outStatus->isSleeping = FALSE;
        outStatus->isCharging = (wiredFound && effBatt < 100);
        outStatus->dpiValue = s_lastKnownDpi;
        outStatus->pollingRateHz = s_lastKnownRateHz;
        wcscpy(outStatus->modeName, L"Bluetooth (BLE)");
        return TRUE;
    }

    // 4. Disconnected
    s_filter.forceDirectReading = FALSE;
    outStatus->mode = MODE_DISCONNECTED;
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_filter.lastKnownBattery;
    outStatus->isCharging = FALSE;
    outStatus->isSleeping = FALSE;
    outStatus->dpiValue = s_lastKnownDpi;
    outStatus->pollingRateHz = s_lastKnownRateHz;
    wcscpy(outStatus->modeName, L"Disconnected");
    return FALSE;
}

BOOL DeviceCoreM1_IsTargetDevice(HANDLE hDevice) {
    return HidCommon_IsTargetDevice(&s_handleCache, hDevice, "3151", s_coreM1Pids, 3);
}

BOOL DeviceCoreM1_HandleRawInput(HRAWINPUT hRaw, BOOL isSleeping, BOOL *outWakeTriggered) {
    if (outWakeTriggered) *outWakeTriggered = FALSE;
    RAWINPUT raw;
    UINT dwSize = sizeof(RAWINPUT);
    if (GetRawInputData(hRaw, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
        if (raw.header.dwType == RIM_TYPEMOUSE) {
            // Strictly check that input came ONLY from the physical Core M1 mouse
            if (DeviceCoreM1_IsTargetDevice(raw.header.hDevice)) {
                DWORD now = GetTickCount();

                // Live polling rate measurement from packet arrival frequency
                if (s_lastMotionTick != 0) {
                    DWORD delta = now - s_lastMotionTick;
                    if (delta >= 1 && delta <= 16) {
                        s_motionIntervalSum += delta;
                        s_motionSampleCount++;
                        if (s_motionSampleCount >= 64) {
                            double avgMs = (double)s_motionIntervalSum / (double)s_motionSampleCount;
                            if (avgMs > 0.0) {
                                int rate = (int)(1000.0 / avgMs + 0.5);
                                if (rate >= 900) s_lastKnownRateHz = 1000;
                                else if (rate >= 450) s_lastKnownRateHz = 500;
                                else if (rate >= 220) s_lastKnownRateHz = 250;
                                else if (rate >= 100) s_lastKnownRateHz = 125;
                            }
                            s_motionIntervalSum = 0;
                            s_motionSampleCount = 0;
                        }
                    }
                }
                s_lastMotionTick = now;

                // If currently sleeping, wake immediately!
                if (isSleeping) {
                    if (outWakeTriggered) *outWakeTriggered = TRUE;
                    s_lastThrottledInputTick = now;
                    return TRUE;
                }

                // If active, throttle timestamp updates to at most once per 1000ms
                // to eliminate CPU load from high-polling gaming mouse packets
                if ((DWORD)(now - s_lastThrottledInputTick) >= 1000) {
                    s_lastThrottledInputTick = now;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

void DeviceCoreM1_Cleanup(void) {
    DeviceCoreM1_InvalidateCache();
}
