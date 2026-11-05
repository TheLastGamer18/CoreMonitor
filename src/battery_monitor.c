#define _WIN32_WINNT 0x0601
#include "battery_monitor.h"
#include <setupapi.h>

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

static HMODULE s_hHid = NULL;
static HidD_GetHidGuid_t s_HidD_GetHidGuid = NULL;
static HidD_GetAttributes_t s_HidD_GetAttributes = NULL;
static HidD_GetPreparsedData_t s_HidD_GetPreparsedData = NULL;
static HidD_FreePreparsedData_t s_HidD_FreePreparsedData = NULL;
static HidP_GetCaps_t s_HidP_GetCaps = NULL;

typedef BOOL (WINAPI *CancelIoEx_t)(HANDLE, LPOVERLAPPED);
static CancelIoEx_t s_fnCancelIoEx = NULL;

static void SafeCancelIo(HANDLE hDev, LPOVERLAPPED lpOverlapped) {
    if (s_fnCancelIoEx) {
        s_fnCancelIoEx(hDev, lpOverlapped);
    } else {
        CancelIo(hDev);
    }
}

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
static int s_lastKnownBattery = -1;
static int s_prev2P4GBatt = -1;
static BOOL s_inferred2P4GCharging = FALSE;
static int s_flatChargeCycles = 0;

static void SaveLastKnownBattery(int percent) {
    if (percent < 0 || percent > 100) return;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\CoreMonitor\\Stats", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = (DWORD)percent;
        RegSetValueExW(hKey, L"LastKnownBattery", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

static void LoadLastKnownBattery(void) {
    HKEY hKey;
    DWORD val = 0;
    DWORD size = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\CoreMonitor\\Stats", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"LastKnownBattery", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            if (val <= 100 && val > 0) s_lastKnownBattery = (int)val;
        }
        RegCloseKey(hKey);
    }
}

static int s_pendingOutlierPercent = -1;
static int s_consecutiveRebounds = 0;
static BOOL s_freshBaselineNeeded = TRUE;
static BOOL s_forceDirectReading = FALSE;

BOOL BatteryMonitor_IsFilteringTransient(void) {
    return (s_pendingOutlierPercent >= 0);
}

void BatteryMonitor_ForceDirectNextPoll(void) {
    s_forceDirectReading = TRUE;
}

void BatteryMonitor_ResetBaseline(void) {
    s_freshBaselineNeeded = TRUE;
    s_pendingOutlierPercent = -1;
    s_consecutiveRebounds = 0;
    s_prev2P4GBatt = -1;
    s_inferred2P4GCharging = FALSE;
    s_flatChargeCycles = 0;
}

static int FilterBatteryReading(int rawPercent, BOOL isCharging) {
    if (rawPercent < 0 || rawPercent > 100) return s_lastKnownBattery;

    // Direct user-initiated refresh OR fresh start / wake: accept immediately
    if (s_forceDirectReading || s_freshBaselineNeeded || s_lastKnownBattery < 0) {
        s_forceDirectReading = FALSE;
        s_freshBaselineNeeded = FALSE;
        s_pendingOutlierPercent = -1;
        s_consecutiveRebounds = 0;
        s_lastKnownBattery = rawPercent;
        SaveLastKnownBattery(rawPercent);
        return rawPercent;
    }

    int diff = rawPercent - s_lastKnownBattery;
    BOOL isOutlier = FALSE;

    if (!isCharging) {
        // Discharging: sudden drops > 15% or jumps upwards > 10% without charging are boot transients or noise
        if (diff < -15 || diff > 10) {
            isOutlier = TRUE;
        } else if (diff > 0) {
            // Small upward rebound (+1% to +10%) during discharge: ADC voltage relaxation / sag recovery
            // Require 2 consecutive readings before accepting an upward increase while on battery
            if (++s_consecutiveRebounds < 2) {
                return s_lastKnownBattery;
            }
            s_consecutiveRebounds = 0;
        } else {
            // Normal decrease or stable percentage: reset rebound counter
            s_consecutiveRebounds = 0;
        }
    } else {
        // Charging: sudden drops > 10% or sudden leaps > 25%
        s_consecutiveRebounds = 0;
        if (diff < -10 || diff > 25) {
            isOutlier = TRUE;
        }
    }

    if (isOutlier) {
        // First occurrence: hold onto previous verified reading and wait for confirmation
        if (s_pendingOutlierPercent != rawPercent) {
            s_pendingOutlierPercent = rawPercent;
            return s_lastKnownBattery; // Suppress transient jump!
        }
        // Confirmed on second consecutive reading: genuine change, accept it
        s_pendingOutlierPercent = -1;
    } else {
        s_pendingOutlierPercent = -1;
    }

    s_lastKnownBattery = rawPercent;
    SaveLastKnownBattery(rawPercent);
    return rawPercent;
}

BOOL BatteryMonitor_Init(void) {
    LoadLastKnownBattery();
    s_freshBaselineNeeded = TRUE;
    s_forceDirectReading = FALSE;
    if (!s_hHid) {
        s_hHid = LoadLibraryA("hid.dll");
        if (s_hHid) {
            s_HidD_GetHidGuid = (HidD_GetHidGuid_t)GetProcAddress(s_hHid, "HidD_GetHidGuid");
            s_HidD_GetAttributes = (HidD_GetAttributes_t)GetProcAddress(s_hHid, "HidD_GetAttributes");
            s_HidD_GetPreparsedData = (HidD_GetPreparsedData_t)GetProcAddress(s_hHid, "HidD_GetPreparsedData");
            s_HidD_FreePreparsedData = (HidD_FreePreparsedData_t)GetProcAddress(s_hHid, "HidD_FreePreparsedData");
            s_HidP_GetCaps = (HidP_GetCaps_t)GetProcAddress(s_hHid, "HidP_GetCaps");
        }
    }
    if (!s_hSetup) {
        s_hSetup = LoadLibraryA("setupapi.dll");
        if (s_hSetup) {
            s_fnGetDevicePropertyW = (SetupDiGetDevicePropertyW_t)GetProcAddress(s_hSetup, "SetupDiGetDevicePropertyW");
        }
    }
    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    if (hKernel) {
        s_fnCancelIoEx = (CancelIoEx_t)GetProcAddress(hKernel, "CancelIoEx");
    }
    return (s_hHid != NULL);
}

void BatteryMonitor_InvalidateCache(void) {
    s_cachedWiredPath[0] = '\0';
    s_cached2P4GPath[0] = '\0';
    s_pendingOutlierPercent = -1;
    s_consecutiveRebounds = 0;
    s_freshBaselineNeeded = TRUE;
    s_prev2P4GBatt = -1;
    s_inferred2P4GCharging = FALSE;
    s_flatChargeCycles = 0;
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

// Queries VIA battery percentage (Channel 0x00, Value 0x07)
// Strictly read-only, non-blocking overlapped I/O with concise, safe timeouts
static int QueryViaBattery(const char *devicePath, BOOL *outIsCharging) {
    if (!devicePath || devicePath[0] == '\0') return -1;
    if (outIsCharging) *outIsCharging = FALSE;

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

    // ReportID: 0x00, id_custom_get_value: 0x08, channel: 0x00, valueId: 0x07 (Battery)
    unsigned char tx[33] = {0};
    tx[0] = 0x00;
    tx[1] = 0x08;
    tx[2] = 0x00;
    tx[3] = 0x07;

    DWORD written = 0;
    BOOL writeOk = FALSE;
    BOOL wr = WriteFile(hDev, tx, 33, &written, &ov);
    if (wr) {
        writeOk = TRUE;
    } else if (GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 200) == WAIT_OBJECT_0) {
            writeOk = GetOverlappedResult(hDev, &ov, &written, FALSE);
        } else {
            SafeCancelIo(hDev, &ov);
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
            // 2.4GHz RF turnaround after idle state takes up to ~140ms.
            // 250ms gives plenty of headroom while completing safely without GUI lag.
            if (WaitForSingleObject(ov.hEvent, 250) == WAIT_OBJECT_0) {
                GetOverlappedResult(hDev, &ov, &bytes, FALSE);
            } else {
                SafeCancelIo(hDev, &ov);
            }
        }

        if (bytes >= 4) {
            int val = -1;
            BOOL chg = FALSE;
            if (rx[0] == 0x00 && rx[1] == 0x08 && rx[2] == 0x00 && rx[3] == 0x07) {
                val = (int)rx[4];
                if (bytes >= 6 && rx[5] == 0x01) {
                    chg = TRUE;
                }
            } else if (rx[0] == 0x08 && rx[1] == 0x00 && rx[2] == 0x07) {
                val = (int)rx[3];
                if (bytes >= 5 && rx[4] == 0x01) {
                    chg = TRUE;
                }
            }
            if (val > 100 && (val & 0x80)) {
                chg = TRUE;
                val = val & 0x7F;
            }
            if (outIsCharging) *outIsCharging = chg;
            if (val > 100) val = 100;
            if (val < 0) val = -1;
            result = val;
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(hDev);
    return result;
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

    BYTE detailBuffer[1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)detailBuffer;
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &devData); i++) {
        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, detail, sizeof(detailBuffer), NULL, NULL)) {
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

                                if (queryBattery) {
                                    resultBattery = QueryViaBattery(detail->DevicePath, NULL);
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

BOOL BatteryMonitor_Poll(URXStatus *outStatus, BOOL allowWirelessQuery) {
    if (!outStatus) return FALSE;
    memset(outStatus, 0, sizeof(URXStatus));
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_lastKnownBattery;

    // 1. Priority 1: Check Wired Mode (VID 0x36B0, PID 0x3130)
    int wiredBatt = -1;
    BOOL wiredFound = FALSE;

    // Try cached path first for zero overhead
    if (s_cachedWiredPath[0] != '\0') {
        HANDLE hTest = CreateFileA(s_cachedWiredPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hTest != INVALID_HANDLE_VALUE) {
            wiredFound = TRUE;
            CloseHandle(hTest);
            wiredBatt = QueryViaBattery(s_cachedWiredPath, NULL);
        } else {
            s_cachedWiredPath[0] = '\0';
        }
    }

    if (!wiredFound) {
        wiredBatt = ScanURX_HID(URX_PID_WIRED, &wiredFound, s_cachedWiredPath, sizeof(s_cachedWiredPath), TRUE);
    }

    if (wiredFound && wiredBatt >= 0) {
        int effBatt = FilterBatteryReading(wiredBatt, TRUE);
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
        // Non-blocking zero-access handle check: confirms dongle is plugged into USB bus
        HANDLE hTest = CreateFileA(s_cached2P4GPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hTest != INVALID_HANDLE_VALUE) {
            dongleFound = TRUE;
            CloseHandle(hTest);
        } else {
            s_cached2P4GPath[0] = '\0';
        }
    }

    if (!dongleFound) {
        // Enumerate USB HID devices without querying over-the-air yet
        ScanURX_HID(URX_PID_2P4G, &dongleFound, s_cached2P4GPath, sizeof(s_cached2P4GPath), FALSE);
    }

    if (dongleFound) {
        outStatus->mode = MODE_2P4G;
        wcscpy(outStatus->modeName, L"2.4GHz Wireless");
        outStatus->isSleeping = FALSE;

        BOOL wirelessCharging = FALSE;
        // Query over-the-air only when explicitly permitted (keyboard active / user refresh)
        if (allowWirelessQuery && s_cached2P4GPath[0] != '\0') {
            dongleBatt = QueryViaBattery(s_cached2P4GPath, &wirelessCharging);
        }

        int effBatt = s_lastKnownBattery;
        if (dongleBatt >= 0) {
            effBatt = FilterBatteryReading(dongleBatt, s_inferred2P4GCharging || wirelessCharging || wiredFound);
            outStatus->batteryPercent = effBatt;
            outStatus->lastKnownPercent = effBatt;

            // Trend-based charging detection for 2.4GHz mode (Approach 2):
            // The firmware strictly decrements during discharge; an increase indicates active charging.
            if (effBatt >= 0) {
                if (s_prev2P4GBatt >= 0) {
                    if (effBatt > s_prev2P4GBatt) {
                        s_inferred2P4GCharging = TRUE;
                        s_flatChargeCycles = 0;
                    } else if (effBatt < s_prev2P4GBatt) {
                        s_inferred2P4GCharging = FALSE;
                        s_flatChargeCycles = 0;
                    } else if (s_inferred2P4GCharging) {
                        // Flat percentage while charging:
                        // If 3 consecutive cycles (15 min at 5-min intervals) pass without an increase,
                        // conclude that the cable has been unplugged.
                        if (++s_flatChargeCycles >= 3) {
                            s_inferred2P4GCharging = FALSE;
                            s_flatChargeCycles = 0;
                        }
                    }
                }
                s_prev2P4GBatt = effBatt;
            }
        } else {
            outStatus->batteryPercent = effBatt;
            outStatus->lastKnownPercent = effBatt;
            s_forceDirectReading = FALSE;
        }

        if (effBatt >= 100) {
            s_inferred2P4GCharging = FALSE;
            s_flatChargeCycles = 0;
        }

        // Charging detection in 2.4GHz mode:
        // Either the wired cable is connected to the PC (wiredFound == TRUE),
        // or over-the-air firmware telemetry indicates charging,
        // or battery trend exhibits confirmed upward increase while < 100.
        BOOL isCharging = (wirelessCharging || wiredFound || s_inferred2P4GCharging) && (effBatt >= 0 && effBatt < 100);
        outStatus->isCharging = isCharging;

        return (dongleBatt >= 0);
    }

    // 3. Priority 3: Check Bluetooth Mode
    int btBatt = ScanURX_Bluetooth();
    if (btBatt >= 0) {
        int effBatt = FilterBatteryReading(btBatt, FALSE);
        outStatus->mode = MODE_BLUETOOTH;
        outStatus->batteryPercent = effBatt;
        outStatus->lastKnownPercent = effBatt;
        outStatus->isSleeping = FALSE;
        outStatus->isCharging = (wiredFound && effBatt < 100);
        wcscpy(outStatus->modeName, L"Bluetooth (BLE)");
        return TRUE;
    }

    // 4. Disconnected
    s_forceDirectReading = FALSE;
    outStatus->mode = MODE_DISCONNECTED;
    outStatus->batteryPercent = -1;
    outStatus->lastKnownPercent = s_lastKnownBattery;
    outStatus->isCharging = FALSE;
    outStatus->isSleeping = FALSE;
    wcscpy(outStatus->modeName, L"Disconnected");
    return FALSE;
}
