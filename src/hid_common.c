#define _WIN32_WINNT 0x0601
#include "hid_common.h"
#include <setupapi.h>
#include <stdio.h>
#include <wctype.h>

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
typedef BOOLEAN (__stdcall *HidD_GetFeature_t)(HANDLE, PVOID, ULONG);
typedef BOOLEAN (__stdcall *HidD_SetFeature_t)(HANDLE, PVOID, ULONG);

static HMODULE s_hHid = NULL;
static HidD_GetHidGuid_t s_HidD_GetHidGuid = NULL;
static HidD_GetAttributes_t s_HidD_GetAttributes = NULL;
static HidD_GetPreparsedData_t s_HidD_GetPreparsedData = NULL;
static HidD_FreePreparsedData_t s_HidD_FreePreparsedData = NULL;
static HidP_GetCaps_t s_HidP_GetCaps = NULL;
static HidD_GetFeature_t s_HidD_GetFeature = NULL;
static HidD_SetFeature_t s_HidD_SetFeature = NULL;

#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
typedef BOOL (WINAPI *CancelIoEx_t)(HANDLE, LPOVERLAPPED);
static CancelIoEx_t s_pfnCancelIoEx = NULL;

static void SafeCancelIo(HANDLE hDev, LPOVERLAPPED lpOverlapped) {
    if (s_pfnCancelIoEx) {
        s_pfnCancelIoEx(hDev, lpOverlapped);
    } else {
        CancelIo(hDev);
    }
}
#endif

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

BOOL HidCommon_Init(void) {
    if (!s_hHid) {
        s_hHid = LoadLibraryA("hid.dll");
        if (s_hHid) {
            s_HidD_GetHidGuid = (HidD_GetHidGuid_t)GetProcAddress(s_hHid, "HidD_GetHidGuid");
            s_HidD_GetAttributes = (HidD_GetAttributes_t)GetProcAddress(s_hHid, "HidD_GetAttributes");
            s_HidD_GetPreparsedData = (HidD_GetPreparsedData_t)GetProcAddress(s_hHid, "HidD_GetPreparsedData");
            s_HidD_FreePreparsedData = (HidD_FreePreparsedData_t)GetProcAddress(s_hHid, "HidD_FreePreparsedData");
            s_HidP_GetCaps = (HidP_GetCaps_t)GetProcAddress(s_hHid, "HidP_GetCaps");
            s_HidD_GetFeature = (HidD_GetFeature_t)GetProcAddress(s_hHid, "HidD_GetFeature");
            s_HidD_SetFeature = (HidD_SetFeature_t)GetProcAddress(s_hHid, "HidD_SetFeature");
        }
    }
    if (!s_fnGetDevicePropertyW) {
        HMODULE hSetup = GetModuleHandleA("setupapi.dll");
        if (hSetup) {
            s_fnGetDevicePropertyW = (SetupDiGetDevicePropertyW_t)GetProcAddress(hSetup, "SetupDiGetDevicePropertyW");
        }
    }
#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
    if (!s_pfnCancelIoEx) {
        HMODULE hKernel = GetModuleHandleA("kernel32.dll");
        if (hKernel) {
            s_pfnCancelIoEx = (CancelIoEx_t)GetProcAddress(hKernel, "CancelIoEx");
        }
    }
#endif
    return (s_hHid != NULL);
}

void HidCommon_Cleanup(void) {
    if (s_hHid) {
        FreeLibrary(s_hHid);
        s_hHid = NULL;
    }
}

BOOL HidCommon_TestDevicePresence(const char *devicePath) {
    if (!devicePath || devicePath[0] == '\0') return FALSE;
    HANDLE hTest = CreateFileA(devicePath, 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hTest != INVALID_HANDLE_VALUE) {
        CloseHandle(hTest);
        return TRUE;
    }
    return FALSE;
}

// -----------------------------------------------------------------------------
// Unified Device Handle Caching & Raw Input Target Filtering
// -----------------------------------------------------------------------------
void HidCommon_ClearHandleCache(DeviceHandleCache *cache) {
    if (!cache) return;
    memset(cache, 0, sizeof(DeviceHandleCache));
}

#ifndef RIDI_DEVICENAME
#define RIDI_DEVICENAME 0x20000007
#endif

BOOL HidCommon_IsTargetDevice(
    DeviceHandleCache *cache,
    HANDLE hDevice,
    const char *vidLower,
    const char **pidListLower,
    int pidCount
) {
    if (!cache || !hDevice || !vidLower || !pidListLower || pidCount <= 0) return FALSE;

    for (int i = 0; i < cache->targetCount; i++) {
        if (cache->targets[i] == hDevice) return TRUE;
    }
    for (int i = 0; i < cache->nonTargetCount; i++) {
        if (cache->nonTargets[i] == hDevice) return FALSE;
    }

    char name[512] = {0};
    UINT size = sizeof(name);
    UINT ret = GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, name, &size);
    if (ret != (UINT)-1 && ret > 0) {
        // Convert to lowercase in-place for fast case-insensitive checks
        for (int i = 0; name[i]; i++) {
            if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 32;
        }

        BOOL isVidMatch = (strstr(name, vidLower) != NULL);
        BOOL isPidMatch = FALSE;
        for (int p = 0; p < pidCount; p++) {
            if (pidListLower[p] && strstr(name, pidListLower[p]) != NULL) {
                isPidMatch = TRUE;
                break;
            }
        }

        if (isVidMatch && isPidMatch) {
            if (cache->targetCount < MAX_CACHED_TARGET_DEVICES) {
                cache->targets[cache->targetCount++] = hDevice;
            } else {
                cache->targets[0] = hDevice;
            }
            return TRUE;
        } else {
            if (cache->nonTargetCount < MAX_CACHED_NON_TARGET_DEVICES) {
                cache->nonTargets[cache->nonTargetCount++] = hDevice;
            } else {
                cache->nonTargets[cache->nonTargetEvictIdx] = hDevice;
                cache->nonTargetEvictIdx = (cache->nonTargetEvictIdx + 1) % MAX_CACHED_NON_TARGET_DEVICES;
            }
            return FALSE;
        }
    }
    return FALSE;
}

// -----------------------------------------------------------------------------
// Unified Battery Filtering & Registry Persistence
// -----------------------------------------------------------------------------
static void SaveBatteryToReg(int percent, const WCHAR *regKey) {
    if (percent < 0 || percent > 100) return;
    if (regKey && regKey[0] != L'\0') {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, regKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            DWORD val = (DWORD)percent;
            RegSetValueExW(hKey, L"LastKnownBattery", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
            RegCloseKey(hKey);
        }
    }
}

static int LoadBatteryFromReg(const WCHAR *regKey, const WCHAR *legacyKey) {
    HKEY hKey;
    DWORD val = 0, size = sizeof(val);
    const WCHAR *keys[2] = { regKey, legacyKey };
    for (int k = 0; k < 2; k++) {
        if (!keys[k] || keys[k][0] == L'\0') continue;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, keys[k], 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            if (RegQueryValueExW(hKey, L"LastKnownBattery", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                if (val <= 100 && val > 0) return (int)val;
            }
            RegCloseKey(hKey);
        }
    }
    return -1;
}

void HidCommon_InitFilter(DeviceFilterState *st, const WCHAR *regKey, const WCHAR *legacyKey) {
    if (!st) return;
    memset(st, 0, sizeof(DeviceFilterState));
    st->lastKnownBattery = LoadBatteryFromReg(regKey, legacyKey);
    st->pendingOutlierPercent = -1;
    st->prev2P4GBatt = -1;
    st->freshBaselineNeeded = TRUE;
}

void HidCommon_ResetFilter(DeviceFilterState *st) {
    if (!st) return;
    st->pendingOutlierPercent = -1;
    st->consecutiveRebounds = 0;
    st->freshBaselineNeeded = TRUE;
}

int HidCommon_FilterBatteryReading(
    DeviceFilterState *st,
    int rawPercent,
    BOOL isCharging,
    const WCHAR *regKey,
    const WCHAR *legacyKey
) {
    if (!st) return rawPercent;
    if (rawPercent < 0 || rawPercent > 100) return st->lastKnownBattery;

    if (st->forceDirectReading || st->freshBaselineNeeded || st->lastKnownBattery < 0) {
        st->forceDirectReading = FALSE;
        st->freshBaselineNeeded = FALSE;
        st->pendingOutlierPercent = -1;
        st->consecutiveRebounds = 0;
        st->lastKnownBattery = rawPercent;
        SaveBatteryToReg(rawPercent, regKey);
        return rawPercent;
    }

    int diff = rawPercent - st->lastKnownBattery;
    BOOL isOutlier = FALSE;

    if (!isCharging) {
        if (diff < -15 || diff > 10) {
            isOutlier = TRUE;
        } else if (diff > 0) {
            if (++st->consecutiveRebounds < 2) {
                return st->lastKnownBattery;
            }
            st->consecutiveRebounds = 0;
        } else {
            st->consecutiveRebounds = 0;
        }
    } else {
        st->consecutiveRebounds = 0;
        if (diff < -10 || diff > 25) {
            isOutlier = TRUE;
        }
    }

    if (isOutlier) {
        if (st->pendingOutlierPercent != rawPercent) {
            st->pendingOutlierPercent = rawPercent;
            return st->lastKnownBattery;
        }
        st->pendingOutlierPercent = -1;
    } else {
        st->pendingOutlierPercent = -1;
    }

    st->lastKnownBattery = rawPercent;
    SaveBatteryToReg(rawPercent, regKey);
    return rawPercent;
}

BOOL HidCommon_UpdateChargeTrend(
    DeviceFilterState *st,
    int effBatt,
    BOOL liveChargingIndicator,
    BOOL wiredConnected
) {
    if (!st) return FALSE;

    if (wiredConnected || liveChargingIndicator) {
        st->inferred2P4GCharging = TRUE;
        st->chargeConfirmationCount = 0;
        st->flatChargeCycles = 0;
        st->prev2P4GBatt = effBatt;
        return (effBatt >= 0 && effBatt < 100);
    }

    if (effBatt >= 0) {
        if (st->prev2P4GBatt >= 0) {
            int delta = effBatt - st->prev2P4GBatt;
            if (delta > 0) {
                if (st->inferred2P4GCharging) {
                    st->flatChargeCycles = 0;
                } else if (delta >= 2) {
                    st->inferred2P4GCharging = TRUE;
                    st->chargeConfirmationCount = 0;
                    st->flatChargeCycles = 0;
                } else {
                    if (++st->chargeConfirmationCount >= 2) {
                        st->inferred2P4GCharging = TRUE;
                        st->chargeConfirmationCount = 0;
                        st->flatChargeCycles = 0;
                    }
                }
            } else if (delta < 0) {
                st->inferred2P4GCharging = FALSE;
                st->chargeConfirmationCount = 0;
                st->flatChargeCycles = 0;
            } else {
                if (st->inferred2P4GCharging) {
                    if (++st->flatChargeCycles >= 30) {
                        st->inferred2P4GCharging = FALSE;
                        st->flatChargeCycles = 0;
                    }
                } else {
                    if (st->chargeConfirmationCount > 0 && ++st->flatChargeCycles >= 2) {
                        st->chargeConfirmationCount = 0;
                        st->flatChargeCycles = 0;
                    }
                }
            }
        }
        st->prev2P4GBatt = effBatt;
    }

    if (effBatt >= 100) {
        st->inferred2P4GCharging = FALSE;
        st->chargeConfirmationCount = 0;
        st->flatChargeCycles = 0;
    }

    return st->inferred2P4GCharging && (effBatt >= 0 && effBatt < 100);
}

// -----------------------------------------------------------------------------
// Core75 VIA Battery Query
// -----------------------------------------------------------------------------
#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
int HidCommon_QueryViaBattery(const char *devicePath, BOOL *outIsCharging) {
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

    BYTE tx[33] = {0};
    tx[0] = 0x00;
    tx[1] = 0x08;
    tx[2] = 0x00;
    tx[3] = 0x07;

    int result = -1;
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
#endif

// -----------------------------------------------------------------------------
// CoreM1 Rongyuan RY6601 Battery & Telemetry Query
// -----------------------------------------------------------------------------
#if defined(BUILD_TARGET_COREM1) || (!defined(BUILD_TARGET_CORE75))
int HidCommon_QueryCoreM1Battery(
    const char *devicePath,
    BOOL isWired,
    BOOL *outIsCharging,
    BOOL *outIsSleeping,
    int *outPollingRate
) {
    if (!devicePath || devicePath[0] == '\0') return -1;
    if (outIsCharging) *outIsCharging = FALSE;
    if (outIsSleeping) *outIsSleeping = FALSE;

    HANDLE hDev = CreateFileA(devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        hDev = CreateFileA(devicePath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);
    }

    if (hDev == INVALID_HANDLE_VALUE) {
        hDev = CreateFileA(devicePath, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);
        if (hDev == INVALID_HANDLE_VALUE) return -1;
    }

    USHORT featLen = 65;
    if (s_HidD_GetPreparsedData && s_HidP_GetCaps) {
        void *preparsed = NULL;
        if (s_HidD_GetPreparsedData(hDev, &preparsed)) {
            HIDP_CAPS caps = {0};
            if (s_HidP_GetCaps(preparsed, &caps)) {
                if (caps.FeatureReportByteLength > 0) {
                    featLen = caps.FeatureReportByteLength;
                }
            }
            if (s_HidD_FreePreparsedData) s_HidD_FreePreparsedData(preparsed);
        }
    }

    int result = -1;
    BOOL isCharging = FALSE;
    BOOL isSleeping = FALSE;
    DWORD reportSize = (featLen <= 128) ? featLen : 128;

    if (!isWired) {
        // -----------------------------------------------------------------
        // 2.4G Receiver Query: Command 0xF7 (Status / Telemetry Poll)
        // -----------------------------------------------------------------
        if (s_HidD_SetFeature && s_HidD_GetFeature && featLen >= 8) {
            BYTE txBuf[128] = {0};
            if (reportSize > 64) {
                txBuf[0] = 0x00;
                txBuf[1] = 0xF7;
            } else {
                txBuf[0] = 0xF7;
            }

            s_HidD_SetFeature(hDev, txBuf, reportSize);
            Sleep(20);

            BYTE rxBuf[128] = {0};
            rxBuf[0] = 0x00;
            if (s_HidD_GetFeature(hDev, rxBuf, reportSize)) {
                int offset = (rxBuf[0] == 0x00 && reportSize > 64) ? 1 : 0;
                int mBatt = (int)rxBuf[offset + 2];
                int mDisc = (int)rxBuf[offset + 4];

                if (mDisc == 1) {
                    isSleeping = TRUE;
                }

                if (mBatt > 100 && (mBatt & 0x80)) {
                    isCharging = TRUE;
                    mBatt &= 0x7F;
                }

                if (mBatt > 0 && mBatt <= 100) {
                    result = mBatt;
                } else if (mBatt == 0 && !isSleeping) {
                    result = 0;
                }
            }
        }
    } else {
        // -----------------------------------------------------------------
        // Wired USB Mouse Query: Command 0x8A (with Checksum 0x75 at byte 7)
        // -----------------------------------------------------------------
        if (s_HidD_SetFeature && s_HidD_GetFeature && featLen >= 8) {
            BYTE txBuf[128] = {0};
            if (reportSize > 64) {
                txBuf[0] = 0x00;
                txBuf[1] = 0x8A;
                txBuf[8] = 0x75;
            } else {
                txBuf[0] = 0x8A;
                txBuf[7] = 0x75;
            }

            s_HidD_SetFeature(hDev, txBuf, reportSize);
            Sleep(20);

            BYTE rxBuf[128] = {0};
            rxBuf[0] = 0x00;
            if (s_HidD_GetFeature(hDev, rxBuf, reportSize)) {
                int offset = (rxBuf[0] == 0x00 && reportSize > 64) ? 1 : 0;
                int cmd = rxBuf[offset + 0];
                int bVal = rxBuf[offset + 1];
                int chgVal = rxBuf[offset + 2];

                if (cmd == 0x0A || cmd == 0x8A) {
                    if (bVal >= 0 && bVal <= 100) {
                        result = bVal;
                        if (chgVal == 1 || bVal < 100) isCharging = TRUE;
                    }
                } else if (bVal >= 0 && bVal <= 100) {
                    result = bVal;
                    if (bVal < 100) isCharging = TRUE;
                }
            }

            // Fallback: also try 0xF7 if 0x8A did not return a valid percentage
            if (result < 0) {
                memset(txBuf, 0, sizeof(txBuf));
                if (reportSize > 64) {
                    txBuf[0] = 0x00;
                    txBuf[1] = 0xF7;
                } else {
                    txBuf[0] = 0xF7;
                }
                s_HidD_SetFeature(hDev, txBuf, reportSize);
                Sleep(20);
                memset(rxBuf, 0, sizeof(rxBuf));
                rxBuf[0] = 0x00;
                if (s_HidD_GetFeature(hDev, rxBuf, reportSize)) {
                    int offset = (rxBuf[0] == 0x00 && reportSize > 64) ? 1 : 0;
                    int mBatt = (int)rxBuf[offset + 2];
                    if (mBatt >= 0 && mBatt <= 100) {
                        result = mBatt;
                        if (mBatt < 100) isCharging = TRUE;
                    }
                }
            }

            // Read Wired Polling Rate via Command 0x88 (Checksum 0x77 at byte 7)
            if (outPollingRate && s_HidD_SetFeature && s_HidD_GetFeature) {
                memset(txBuf, 0, sizeof(txBuf));
                if (reportSize > 64) {
                    txBuf[0] = 0x00; txBuf[1] = 0x88; txBuf[8] = 0x77;
                } else {
                    txBuf[0] = 0x88; txBuf[7] = 0x77;
                }
                s_HidD_SetFeature(hDev, txBuf, reportSize);
                Sleep(15);
                memset(rxBuf, 0, sizeof(rxBuf));
                rxBuf[0] = 0x00;
                if (s_HidD_GetFeature(hDev, rxBuf, reportSize)) {
                    int offset = (rxBuf[0] == 0x00 && reportSize > 64) ? 1 : 0;
                    if (rxBuf[offset + 0] == 0x08 || rxBuf[offset + 0] == 0x88) {
                        BYTE code = rxBuf[offset + 1];
                        static const int RATE_TABLE[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
                        if (code < sizeof(RATE_TABLE)/sizeof(RATE_TABLE[0])) {
                            *outPollingRate = RATE_TABLE[code];
                        }
                    }
                }
            }
        }
    }

    CloseHandle(hDev);

    if (outIsCharging) *outIsCharging = isCharging;
    if (outIsSleeping) *outIsSleeping = isSleeping;
    return result;
}
#endif

// -----------------------------------------------------------------------------
// PnP / HID Enumeration
// -----------------------------------------------------------------------------
BOOL HidCommon_ScanDevice(
    USHORT targetVid,
    USHORT targetPid,
    USHORT targetUsagePage,
    USHORT targetUsage,
    char *outPath,
    DWORD pathMax
) {
    if (!s_HidD_GetHidGuid) return FALSE;

    GUID hidGuid;
    s_HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return FALSE;

    SP_DEVICE_INTERFACE_DATA devData;
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    BOOL found = FALSE;

    BYTE detailBuffer[1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)detailBuffer;
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

    char vidStrLower[16], vidStrUpper[16];
    _snprintf(vidStrLower, sizeof(vidStrLower), "%04x", targetVid);
    _snprintf(vidStrUpper, sizeof(vidStrUpper), "%04X", targetVid);

    char pidStrLower[16], pidStrUpper[16];
    _snprintf(pidStrLower, sizeof(pidStrLower), "%04x", targetPid);
    _snprintf(pidStrUpper, sizeof(pidStrUpper), "%04X", targetPid);

    char fallbackPath[MAX_PATH * 2] = {0};

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &devData); i++) {
        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, detail, sizeof(detailBuffer), NULL, NULL)) {
            if ((strstr(detail->DevicePath, vidStrLower) || strstr(detail->DevicePath, vidStrUpper)) &&
                (strstr(detail->DevicePath, pidStrLower) || strstr(detail->DevicePath, pidStrUpper))) {
                HANDLE hDev = CreateFileA(detail->DevicePath, 0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);

                if (hDev != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES attr = {0};
                    attr.Size = sizeof(HIDD_ATTRIBUTES);
                    if (s_HidD_GetAttributes(hDev, &attr) && attr.VendorID == targetVid && attr.ProductID == targetPid) {
                        void *preparsed = NULL;
                        if (s_HidD_GetPreparsedData(hDev, &preparsed)) {
                            HIDP_CAPS caps = {0};
                            s_HidP_GetCaps(preparsed, &caps);

                            if (targetUsagePage != 0) {
                                if (caps.UsagePage == targetUsagePage && (targetUsage == 0 || caps.Usage == targetUsage)) {
                                    found = TRUE;
                                    if (outPath && pathMax > 0) {
                                        strncpy(outPath, detail->DevicePath, pathMax - 1);
                                        outPath[pathMax - 1] = '\0';
                                    }
                                    s_HidD_FreePreparsedData(preparsed);
                                    CloseHandle(hDev);
                                    break;
                                }
                            } else {
                                if (caps.UsagePage >= 0xFF00 && caps.FeatureReportByteLength >= 8) {
                                    found = TRUE;
                                    if (outPath && pathMax > 0) {
                                        strncpy(outPath, detail->DevicePath, pathMax - 1);
                                        outPath[pathMax - 1] = '\0';
                                    }
                                    s_HidD_FreePreparsedData(preparsed);
                                    CloseHandle(hDev);
                                    break;
                                } else if (caps.FeatureReportByteLength >= 8 && (caps.UsagePage != 0x01 || (caps.Usage != 0x02 && caps.Usage != 0x06))) {
                                    if (fallbackPath[0] == '\0') {
                                        strncpy(fallbackPath, detail->DevicePath, sizeof(fallbackPath) - 1);
                                    }
                                }
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

    if (!found && fallbackPath[0] != '\0') {
        found = TRUE;
        if (outPath && pathMax > 0) {
            strncpy(outPath, fallbackPath, pathMax - 1);
            outPath[pathMax - 1] = '\0';
        }
    }

    return found;
}

// -----------------------------------------------------------------------------
// Case-Insensitive Bluetooth BLE Battery Query
// -----------------------------------------------------------------------------
int HidCommon_ScanBluetoothBattery(const WCHAR *filter1, const WCHAR *filter2) {
    if (!s_fnGetDevicePropertyW) return -1;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return -1;

    SP_DEVINFO_DATA devData = {0};
    devData.cbSize = sizeof(devData);

    WCHAR lowerFilter1[128] = {0};
    WCHAR lowerFilter2[128] = {0};
    if (filter1) {
        for (int j = 0; filter1[j] && j < 127; j++) lowerFilter1[j] = towlower(filter1[j]);
    }
    if (filter2) {
        for (int j = 0; filter2[j] && j < 127; j++) lowerFilter2[j] = towlower(filter2[j]);
    }

    int battery = -1;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
        WCHAR name[256] = {0};
        DEVPROPTYPE propType;
        DWORD reqSize = 0;
        if (s_fnGetDevicePropertyW(hDevInfo, &devData, &PKEY_Device_FriendlyName, &propType, (PBYTE)name, sizeof(name), &reqSize, 0)) {
            WCHAR lowerName[256] = {0};
            for (int j = 0; name[j] && j < 255; j++) lowerName[j] = towlower(name[j]);

            BOOL matches1 = (filter1 == NULL || wcsstr(lowerName, lowerFilter1) != NULL);
            BOOL matches2 = (filter2 == NULL || wcsstr(lowerName, lowerFilter2) != NULL);
            if (matches1 && matches2) {
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
