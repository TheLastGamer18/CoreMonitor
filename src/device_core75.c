#define _WIN32_WINNT 0x0601
#include "device_core75.h"
#include "hid_common.h"
#include <stdio.h>

#define CORE75_VID       0x36B0
#define CORE75_PID_WIRED 0x3130
#define CORE75_PID_2P4G  0x3002

#define RAW_USAGE_PAGE 0xFF60
#define RAW_USAGE      0x0061

#define REG_CORE75_KEY L"Software\\CoreMonitor\\Stats\\Core75"
#define REG_LEGACY_KEY L"Software\\CoreMonitor\\Stats"

static char s_cachedWiredPath[MAX_PATH * 2] = {0};
static char s_cached2P4GPath[MAX_PATH * 2] = {0};

static DeviceFilterState s_filter = {0};
static DeviceHandleCache s_handleCache = {0};
static const char *s_core75Pids[] = { "3130", "3002" };
static BOOL s_isCapsLockOn = FALSE;

void DeviceCore75_Init(void) {
    HidCommon_InitFilter(&s_filter, REG_CORE75_KEY, REG_LEGACY_KEY);
    HidCommon_ClearHandleCache(&s_handleCache);
    s_isCapsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

void DeviceCore75_InvalidateCache(void) {
    s_cachedWiredPath[0] = '\0';
    s_cached2P4GPath[0] = '\0';
    HidCommon_ResetFilter(&s_filter);
    HidCommon_ClearHandleCache(&s_handleCache);
}

BOOL DeviceCore75_IsFilteringTransient(void) {
    return (s_filter.pendingOutlierPercent >= 0);
}

void DeviceCore75_ForceDirectNextPoll(void) {
    s_filter.forceDirectReading = TRUE;
}

void DeviceCore75_ResetBaseline(void) {
    HidCommon_ResetFilter(&s_filter);
}

BOOL DeviceCore75_Poll(DeviceStatus *outStatus, BOOL allowWirelessQuery) {
    if (!outStatus) return FALSE;
    memset(outStatus, 0, sizeof(DeviceStatus));
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_filter.lastKnownBattery;
    outStatus->pollingRateHz = 1000;
    s_isCapsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    outStatus->isCapsLock = s_isCapsLockOn;

    // 1. Priority 1: Check Wired Mode (VID 0x36B0, PID 0x3130)
    int wiredBatt = -1;
    BOOL wiredFound = FALSE;

    if (s_cachedWiredPath[0] != '\0') {
        if (HidCommon_TestDevicePresence(s_cachedWiredPath)) {
            wiredFound = TRUE;
            BOOL dummyChg = FALSE;
            wiredBatt = HidCommon_QueryViaBattery(s_cachedWiredPath, &dummyChg);
        } else {
            s_cachedWiredPath[0] = '\0';
        }
    }

    if (!wiredFound) {
        wiredFound = HidCommon_ScanDevice(CORE75_VID, CORE75_PID_WIRED, RAW_USAGE_PAGE, RAW_USAGE, s_cachedWiredPath, sizeof(s_cachedWiredPath));
        if (wiredFound) {
            BOOL dummyChg = FALSE;
            wiredBatt = HidCommon_QueryViaBattery(s_cachedWiredPath, &dummyChg);
        }
    }

    if (wiredFound && wiredBatt >= 0) {
        int effBatt = HidCommon_FilterBatteryReading(&s_filter, wiredBatt, TRUE, REG_CORE75_KEY, REG_LEGACY_KEY);
        outStatus->mode = MODE_WIRED;
        outStatus->batteryPercent = effBatt;
        outStatus->lastKnownPercent = effBatt;
        outStatus->isCharging = (effBatt < 100);
        outStatus->isSleeping = FALSE;
        wcscpy(outStatus->modeName, L"Wired USB");
        return TRUE;
    }

    // 2. Priority 2: Check 2.4GHz Dongle Mode (VID 0x36B0, PID 0x3002)
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
        dongleFound = HidCommon_ScanDevice(CORE75_VID, CORE75_PID_2P4G, RAW_USAGE_PAGE, RAW_USAGE, s_cached2P4GPath, sizeof(s_cached2P4GPath));
    }

    if (dongleFound) {
        outStatus->mode = MODE_2P4G;
        wcscpy(outStatus->modeName, L"2.4GHz Wireless");
        outStatus->isSleeping = FALSE;

        BOOL wirelessCharging = FALSE;
        if (allowWirelessQuery && s_cached2P4GPath[0] != '\0') {
            dongleBatt = HidCommon_QueryViaBattery(s_cached2P4GPath, &wirelessCharging);
        }

        int effBatt = s_filter.lastKnownBattery;
        if (dongleBatt >= 0) {
            effBatt = HidCommon_FilterBatteryReading(&s_filter, dongleBatt, s_filter.inferred2P4GCharging || wirelessCharging || wiredFound, REG_CORE75_KEY, REG_LEGACY_KEY);
            outStatus->batteryPercent = effBatt;
            outStatus->lastKnownPercent = effBatt;
            outStatus->isCharging = HidCommon_UpdateChargeTrend(&s_filter, effBatt, wirelessCharging, wiredFound);
        } else {
            outStatus->batteryPercent = effBatt;
            outStatus->lastKnownPercent = effBatt;
            s_filter.forceDirectReading = FALSE;
            outStatus->isCharging = (s_filter.inferred2P4GCharging || wiredFound) && (effBatt >= 0 && effBatt < 100);
        }

        return (dongleBatt >= 0);
    }

    // 3. Priority 3: Check Bluetooth Mode
    int btBatt = HidCommon_ScanBluetoothBattery(L"Core75", NULL);
    if (btBatt >= 0) {
        int effBatt = HidCommon_FilterBatteryReading(&s_filter, btBatt, FALSE, REG_CORE75_KEY, REG_LEGACY_KEY);
        outStatus->mode = MODE_BLUETOOTH;
        outStatus->batteryPercent = effBatt;
        outStatus->lastKnownPercent = effBatt;
        outStatus->isSleeping = FALSE;
        outStatus->isCharging = (wiredFound && effBatt < 100);
        wcscpy(outStatus->modeName, L"Bluetooth (BLE)");
        return TRUE;
    }

    // 4. Priority 4: Disconnected
    s_filter.forceDirectReading = FALSE;
    s_filter.inferred2P4GCharging = FALSE;
    s_filter.chargeConfirmationCount = 0;
    s_filter.flatChargeCycles = 0;
    s_filter.prev2P4GBatt = -1;
    outStatus->mode = MODE_DISCONNECTED;
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_filter.lastKnownBattery;
    outStatus->isCharging = FALSE;
    outStatus->isSleeping = FALSE;
    wcscpy(outStatus->modeName, L"Disconnected");
    return FALSE;
}

BOOL DeviceCore75_IsTargetDevice(HANDLE hDevice) {
    return HidCommon_IsTargetDevice(&s_handleCache, hDevice, "36b0", s_core75Pids, 2);
}

BOOL DeviceCore75_HandleRawInput(HRAWINPUT hRaw, BOOL *outWakeTriggered, BOOL *outCapsChanged) {
    if (outWakeTriggered) *outWakeTriggered = FALSE;
    if (outCapsChanged) *outCapsChanged = FALSE;
    RAWINPUT raw;
    UINT dwSize = sizeof(RAWINPUT);
    if (GetRawInputData(hRaw, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
        if (raw.header.dwType == RIM_TYPEKEYBOARD) {
            if (DeviceCore75_IsTargetDevice(raw.header.hDevice)) {
                BOOL newCaps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                if (raw.data.keyboard.VKey == VK_CAPITAL && !(raw.data.keyboard.Flags & RI_KEY_BREAK)) {
                    newCaps = !s_isCapsLockOn;
                }
                if (newCaps != s_isCapsLockOn) {
                    s_isCapsLockOn = newCaps;
                    if (outCapsChanged) *outCapsChanged = TRUE;
                }
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL DeviceCore75_IsCapsLock(void) {
    return s_isCapsLockOn;
}

void DeviceCore75_Cleanup(void) {
    DeviceCore75_InvalidateCache();
}
