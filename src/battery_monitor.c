#include "battery_monitor.h"
#include "settings.h"
#include <setupapi.h>
#include <stdio.h>

#define URX_VID 0x36B0
#define URX_PID_WIRED 0x3130
#define URX_PID_2P4G  0x3002

// QMK VIA Raw HID Constants
#define RAW_USAGE_PAGE 0xFF60
#define RAW_USAGE      0x0061

typedef struct _HIDD_ATTRIBUTES {
    ULONG Size;
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
} HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;

typedef USHORT USAGE;
typedef struct _HIDP_CAPS {
    USAGE    Usage;
    USAGE    UsagePage;
    USHORT   InputReportByteLength;
    USHORT   OutputReportByteLength;
    USHORT   FeatureReportByteLength;
    USHORT   Reserved[17];
    USHORT   NumberLinkCollectionNodes;
    USHORT   NumberInputButtonCaps;
    USHORT   NumberInputValueCaps;
    USHORT   NumberInputDataIndices;
    USHORT   NumberOutputButtonCaps;
    USHORT   NumberOutputValueCaps;
    USHORT   NumberOutputDataIndices;
    USHORT   NumberFeatureButtonCaps;
    USHORT   NumberFeatureValueCaps;
    USHORT   NumberFeatureDataIndices;
} HIDP_CAPS, *PHIDP_CAPS;

typedef void (__stdcall *HidD_GetHidGuid_t)(LPGUID);
typedef BOOLEAN (__stdcall *HidD_GetAttributes_t)(HANDLE, PHIDD_ATTRIBUTES);
typedef BOOLEAN (__stdcall *HidD_GetPreparsedData_t)(HANDLE, void**);
typedef BOOLEAN (__stdcall *HidD_FreePreparsedData_t)(void*);
typedef LONG (__stdcall *HidP_GetCaps_t)(void*, PHIDP_CAPS);
typedef BOOLEAN (__stdcall *HidD_SetOutputReport_t)(HANDLE, PVOID, ULONG);

static HMODULE s_hHid = NULL;
static HidD_GetHidGuid_t s_HidD_GetHidGuid = NULL;
static HidD_GetAttributes_t s_HidD_GetAttributes = NULL;
static HidD_GetPreparsedData_t s_HidD_GetPreparsedData = NULL;
static HidD_FreePreparsedData_t s_HidD_FreePreparsedData = NULL;
static HidP_GetCaps_t s_HidP_GetCaps = NULL;
static HidD_SetOutputReport_t s_HidD_SetOutputReport = NULL;

static HMODULE s_hSetup = NULL;
typedef GUID DEVPROPGUID;
typedef ULONG DEVPROPID;
typedef struct _DEVPROPKEY {
    DEVPROPGUID fmtid;
    DEVPROPID   pid;
} DEVPROPKEY;
typedef ULONG DEVPROPTYPE;

typedef BOOL (WINAPI *SetupDiGetDevicePropertyW_t)(
    HDEVINFO DeviceInfoSet,
    PSP_DEVINFO_DATA DeviceInfoData,
    const DEVPROPKEY *PropertyKey,
    DEVPROPTYPE *PropertyType,
    PBYTE PropertyBuffer,
    DWORD PropertyBufferSize,
    PDWORD RequiredSize,
    DWORD Flags
);
static SetupDiGetDevicePropertyW_t s_fnGetDevicePropertyW = NULL;

static const DEVPROPKEY PKEY_Bluetooth_Battery = {
    { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } },
    2
};

static const DEVPROPKEY PKEY_Device_FriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
    14
};

// Cached device interface paths for instant, zero-overhead querying
static char s_cachedWiredPath[MAX_PATH * 2] = {0};
static char s_cached2P4GPath[MAX_PATH * 2] = {0};
static int s_cachedSleepTimerMinutes = 0;
static int s_lastKnownBattery = -1;

BOOL BatteryMonitor_Init(void) {
    if (!s_hHid) {
        s_hHid = LoadLibraryA("hid.dll");
        if (s_hHid) {
            s_HidD_GetHidGuid = (HidD_GetHidGuid_t)GetProcAddress(s_hHid, "HidD_GetHidGuid");
            s_HidD_GetAttributes = (HidD_GetAttributes_t)GetProcAddress(s_hHid, "HidD_GetAttributes");
            s_HidD_GetPreparsedData = (HidD_GetPreparsedData_t)GetProcAddress(s_hHid, "HidD_GetPreparsedData");
            s_HidD_FreePreparsedData = (HidD_FreePreparsedData_t)GetProcAddress(s_hHid, "HidD_FreePreparsedData");
            s_HidP_GetCaps = (HidP_GetCaps_t)GetProcAddress(s_hHid, "HidP_GetCaps");
            s_HidD_SetOutputReport = (HidD_SetOutputReport_t)GetProcAddress(s_hHid, "HidD_SetOutputReport");
        }
    }
    if (!s_hSetup) {
        s_hSetup = LoadLibraryA("setupapi.dll");
        if (s_hSetup) {
            s_fnGetDevicePropertyW = (SetupDiGetDevicePropertyW_t)GetProcAddress(s_hSetup, "SetupDiGetDevicePropertyW");
        }
    }
    return (s_hHid != NULL);
}

static int s_consecutiveFailures = 0;

void BatteryMonitor_InvalidateCache(void) {
    s_cachedWiredPath[0] = '\0';
    s_cached2P4GPath[0] = '\0';
    s_cachedSleepTimerMinutes = 0;
    s_consecutiveFailures = 0;
}

int BatteryMonitor_GetSleepTimerMinutes(void) {
    return (s_cachedSleepTimerMinutes > 0) ? s_cachedSleepTimerMinutes : 3;
}

void BatteryMonitor_Cleanup(void) {
    BatteryMonitor_InvalidateCache();
    if (s_hHid) {
        FreeLibrary(s_hHid);
        s_hHid = NULL;
    }
    if (s_hSetup) {
        FreeLibrary(s_hSetup);
        s_hSetup = NULL;
    }
}

// Unified VIA Raw HID read helper for custom values (id_custom_get_value = 0x08)
// Strictly read-only, non-blocking overlapped I/O with concise, safe timeouts
static int QueryViaCustomValue(const char *devicePath, unsigned char channel, unsigned char valueId) {
    if (!devicePath || devicePath[0] == '\0') return -1;

    HANDLE hDev = CreateFileA(devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        return -1;
    }

    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        CloseHandle(hDev);
        return -1;
    }

    int result = -1;

    // ReportID: 0x00, id_custom_get_value: 0x08, channel, valueId
    unsigned char tx[33] = {0};
    tx[0] = 0x00;
    tx[1] = 0x08;
    tx[2] = channel;
    tx[3] = valueId;

    DWORD written = 0;
    BOOL writeOk = FALSE;
    BOOL wr = WriteFile(hDev, tx, 33, &written, &ov);
    if (wr) {
        writeOk = TRUE;
    } else if (GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 150) == WAIT_OBJECT_0) {
            writeOk = GetOverlappedResult(hDev, &ov, &written, FALSE);
        } else {
            CancelIo(hDev);
        }
    }

    if (writeOk) {
        ResetEvent(ov.hEvent);
        unsigned char rx[65] = {0};
        DWORD bytes = 0;
        BOOL rd = ReadFile(hDev, rx, 33, &bytes, &ov);
        if (rd) {
            GetOverlappedResult(hDev, &ov, &bytes, FALSE);
        } else if (GetLastError() == ERROR_IO_PENDING) {
            // 200ms is ample turnaround for 2.4GHz RF and SPI
            if (WaitForSingleObject(ov.hEvent, 200) == WAIT_OBJECT_0) {
                GetOverlappedResult(hDev, &ov, &bytes, FALSE);
            } else {
                CancelIo(hDev);
            }
        }

        if (bytes >= 4) {
            if (rx[0] == 0x00 && rx[1] == 0x08 && rx[2] == channel && rx[3] == valueId) {
                result = (int)rx[4];
            } else if (rx[0] == 0x08 && rx[1] == channel && rx[2] == valueId) {
                result = (int)rx[3];
            }
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(hDev);
    return result;
}

// Queries VIA battery percentage (Channel 0x00, Value 0x07) with a fast retry
static int QueryViaBattery(const char *devicePath) {
    int b = QueryViaCustomValue(devicePath, 0x00, 0x07);
    if (b < 0) {
        // Fast retry after 40ms to absorb transient RF packet collisions during active typing
        Sleep(40);
        b = QueryViaCustomValue(devicePath, 0x00, 0x07);
    }
    if (b > 100) b = 100;
    if (b < 0) b = -1;
    return b;
}

// Queries configured sleep timer from firmware (Channel 0x00, Value 0x04)
// Maps firmware index (0: 1m, 1: 3m, 2: 10m, 3: 30m) or direct minutes to minute value
static int QueryViaSleepTimer(const char *devicePath) {
    int val = QueryViaCustomValue(devicePath, 0x00, 0x04);
    if (val < 0) return -1;
    switch (val) {
        case 0: return 1;
        case 1: return 3;
        case 2: return 10;
        case 3: return 30;
        default:
            if (val == 1 || val == 3 || val == 10 || val == 30) return val;
            return 3;
    }
}

// Scans HID devices matching URX VID and target PID
static int ScanURX_HID(USHORT targetPid, BOOL *outDeviceFound, char *outPath, DWORD pathMax, BOOL queryBattery) {
    if (!s_HidD_GetHidGuid) return -1;

    GUID hidGuid;
    s_HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return -1;

    SP_DEVICE_INTERFACE_DATA devData;
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    int resultBattery = -1;
    if (outDeviceFound) *outDeviceFound = FALSE;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &devData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, NULL, 0, &reqSize, NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(reqSize);
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, detail, reqSize, NULL, NULL)) {
            if (strstr(detail->DevicePath, "36b0") || strstr(detail->DevicePath, "36B0")) {
                // Open with 0 access to query attributes cleanly without locking device handles
                HANDLE hDev = CreateFileA(detail->DevicePath, 0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);

                if (hDev != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES attr = {0};
                    attr.Size = sizeof(HIDD_ATTRIBUTES);
                    if (s_HidD_GetAttributes(hDev, &attr) && attr.VendorID == URX_VID && attr.ProductID == targetPid) {
                        void *preparsed = NULL;
                        if (s_HidD_GetPreparsedData(hDev, &preparsed)) {
                            HIDP_CAPS caps = {0};
                            s_HidP_GetCaps(preparsed, &caps);
                            if (caps.UsagePage == RAW_USAGE_PAGE && caps.Usage == RAW_USAGE) {
                                if (outDeviceFound) *outDeviceFound = TRUE;
                                if (outPath && pathMax > 0) {
                                    strncpy(outPath, detail->DevicePath, pathMax - 1);
                                    outPath[pathMax - 1] = '\0';
                                }
                                s_HidD_FreePreparsedData(preparsed);
                                CloseHandle(hDev);

                                char devPathCopy[MAX_PATH * 2] = {0};
                                strncpy(devPathCopy, detail->DevicePath, sizeof(devPathCopy) - 1);
                                free(detail);

                                if (queryBattery) {
                                    resultBattery = QueryViaBattery(devPathCopy);
                                }
                                break;
                            }
                            s_HidD_FreePreparsedData(preparsed);
                        }
                    }
                    CloseHandle(hDev);
                }
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return resultBattery;
}

// Scans for URX Core75 Bluetooth connections via Windows PnP
static int ScanURX_Bluetooth(void) {
    if (!s_fnGetDevicePropertyW) return -1;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return -1;

    SP_DEVINFO_DATA devData = {0};
    devData.cbSize = sizeof(devData);

    int battery = -1;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
        WCHAR name[256] = {0};
        DEVPROPTYPE propType;
        DWORD reqSize = 0;
        if (s_fnGetDevicePropertyW(hDevInfo, &devData, &PKEY_Device_FriendlyName, &propType, (PBYTE)name, sizeof(name), &reqSize, 0)) {
            if (wcsstr(name, L"URX") && wcsstr(name, L"Core75")) {
                BYTE bVal = 0;
                if (s_fnGetDevicePropertyW(hDevInfo, &devData, &PKEY_Bluetooth_Battery, &propType, &bVal, sizeof(bVal), &reqSize, 0)) {
                    battery = (int)bVal;
                    if (battery >= 0 && battery <= 100) {
                        break;
                    }
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return battery;
}

void BatteryMonitor_Poll(URXStatus *outStatus, BOOL allowWirelessQuery) {
    if (!outStatus) return;
    memset(outStatus, 0, sizeof(URXStatus));
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_lastKnownBattery;
    outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();

    // 1. Priority 1: Check Wired Mode (VID 0x36B0, PID 0x3130)
    int wiredBatt = -1;
    BOOL wiredFound = FALSE;

    // Try cached path first for zero overhead
    if (s_cachedWiredPath[0] != '\0') {
        wiredBatt = QueryViaBattery(s_cachedWiredPath);
        if (wiredBatt >= 0) {
            wiredFound = TRUE;
        } else {
            // Path may have changed or device unplugged
            s_cachedWiredPath[0] = '\0';
        }
    }

    if (!wiredFound) {
        wiredBatt = ScanURX_HID(URX_PID_WIRED, &wiredFound, s_cachedWiredPath, sizeof(s_cachedWiredPath), TRUE);
    }

    if (wiredFound && wiredBatt >= 0) {
        s_consecutiveFailures = 0;
        s_lastKnownBattery = wiredBatt;
        if (s_cachedSleepTimerMinutes <= 0 && s_cachedWiredPath[0] != '\0') {
            int st = QueryViaSleepTimer(s_cachedWiredPath);
            if (st > 0) s_cachedSleepTimerMinutes = st;
        }
        outStatus->mode = MODE_WIRED;
        outStatus->batteryPercent = wiredBatt;
        outStatus->lastKnownPercent = wiredBatt;
        outStatus->isCharging = (wiredBatt < 100);
        outStatus->isSleeping = FALSE;
        outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();
        wcscpy(outStatus->modeName, L"Wired USB");
        if (outStatus->isCharging) {
            wcscpy(outStatus->stateName, L"Charging");
            _snwprintf(outStatus->statusText, 128, L"URX Core75: %d%% - [Wired - Charging]", wiredBatt);
        } else {
            wcscpy(outStatus->stateName, L"Fully Charged");
            _snwprintf(outStatus->statusText, 128, L"URX Core75: 100%% - [Wired - Fully Charged]");
        }
        return;
    }

    // 2. Priority 2: Check 2.4GHz Dongle Mode (VID 0x36B0, PID 0x3002)
    int dongleBatt = -1;
    BOOL dongleFound = FALSE;

    if (allowWirelessQuery) {
        if (s_cached2P4GPath[0] != '\0') {
            dongleBatt = QueryViaBattery(s_cached2P4GPath);
            if (dongleBatt >= 0) {
                dongleFound = TRUE;
            } else {
                // Check if device still exists even if query timed out (asleep or off)
                HANDLE hTest = CreateFileA(s_cached2P4GPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                if (hTest != INVALID_HANDLE_VALUE) {
                    dongleFound = TRUE;
                    CloseHandle(hTest);
                } else {
                    s_cached2P4GPath[0] = '\0';
                }
            }
        }

        if (!dongleFound) {
            dongleBatt = ScanURX_HID(URX_PID_2P4G, &dongleFound, s_cached2P4GPath, sizeof(s_cached2P4GPath), TRUE);
        }

        if (dongleFound) {
            outStatus->mode = MODE_2P4G;
            wcscpy(outStatus->modeName, L"2.4GHz Wireless");

            if (dongleBatt >= 0) {
                s_consecutiveFailures = 0;
                s_lastKnownBattery = dongleBatt;
                if (s_cachedSleepTimerMinutes <= 0 && s_cached2P4GPath[0] != '\0') {
                    int st = QueryViaSleepTimer(s_cached2P4GPath);
                    if (st > 0) s_cachedSleepTimerMinutes = st;
                }
                outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();
                outStatus->batteryPercent = dongleBatt;
                outStatus->lastKnownPercent = dongleBatt;
                outStatus->isSleeping = FALSE;
                outStatus->isCharging = FALSE;

                wcscpy(outStatus->stateName, L"Active");
                _snwprintf(outStatus->statusText, 128, L"URX Core75: %d%% - [2.4GHz Wireless]", dongleBatt);
            } else {
                s_consecutiveFailures++;
                outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();
                // Filter out single-poll radio latency / transient RF collisions:
                if (s_consecutiveFailures < 2 && s_lastKnownBattery >= 0) {
                    outStatus->batteryPercent = s_lastKnownBattery;
                    outStatus->lastKnownPercent = s_lastKnownBattery;
                    outStatus->isCharging = FALSE;
                    outStatus->isSleeping = FALSE;
                    wcscpy(outStatus->stateName, L"Active");
                    _snwprintf(outStatus->statusText, 128, L"URX Core75: %d%% - [2.4GHz Wireless]", s_lastKnownBattery);
                } else {
                    outStatus->batteryPercent = -1;
                    outStatus->lastKnownPercent = s_lastKnownBattery;
                    outStatus->isCharging = FALSE;
                    outStatus->isSleeping = TRUE;
                    wcscpy(outStatus->stateName, L"Asleep");

                    if (s_lastKnownBattery >= 0) {
                        _snwprintf(outStatus->statusText, 128, L"URX Core75: %d%% - [Asleep - 2.4GHz]", s_lastKnownBattery);
                    } else {
                        _snwprintf(outStatus->statusText, 128, L"URX Core75: Asleep - [2.4GHz]");
                    }
                }
            }
            return;
        }
    } else {
        // Wireless queries suppressed: check dongle USB presence with 0 access (NO over-the-air RF packet sent!)
        if (s_cached2P4GPath[0] != '\0') {
            HANDLE hTest = CreateFileA(s_cached2P4GPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hTest != INVALID_HANDLE_VALUE) {
                dongleFound = TRUE;
                CloseHandle(hTest);
            } else {
                s_cached2P4GPath[0] = '\0';
            }
        }

        if (!dongleFound) {
            ScanURX_HID(URX_PID_2P4G, &dongleFound, s_cached2P4GPath, sizeof(s_cached2P4GPath), FALSE);
        }

        if (dongleFound) {
            outStatus->mode = MODE_2P4G;
            wcscpy(outStatus->modeName, L"2.4GHz Wireless");
            outStatus->batteryPercent = s_lastKnownBattery;
            outStatus->lastKnownPercent = s_lastKnownBattery;
            outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();
            return;
        }
    }

    s_consecutiveFailures = 0;

    // 3. Priority 3: Check Bluetooth Mode
    int btBatt = ScanURX_Bluetooth();
    if (btBatt >= 0) {
        s_lastKnownBattery = btBatt;
        outStatus->mode = MODE_BLUETOOTH;
        outStatus->batteryPercent = btBatt;
        outStatus->lastKnownPercent = btBatt;
        outStatus->isSleeping = FALSE;
        outStatus->isCharging = FALSE;
        outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();
        wcscpy(outStatus->modeName, L"Bluetooth (BLE)");
        wcscpy(outStatus->stateName, L"Active");
        _snwprintf(outStatus->statusText, 128, L"URX Core75: %d%% - [Bluetooth]", btBatt);
        return;
    }

    // 4. Disconnected
    outStatus->mode = MODE_DISCONNECTED;
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_lastKnownBattery;
    outStatus->isCharging = FALSE;
    outStatus->isSleeping = FALSE;
    outStatus->sleepTimerMinutes = BatteryMonitor_GetSleepTimerMinutes();
    wcscpy(outStatus->modeName, L"Disconnected");
    wcscpy(outStatus->stateName, L"Offline");
    _snwprintf(outStatus->statusText, 128, L"URX Core75: Disconnected");
}
