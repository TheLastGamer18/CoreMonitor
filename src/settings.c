#include "settings.h"
#include <stdio.h>

#define REG_ROOT_SUBKEY   L"Software\\CoreMonitor"
#define REG_CORE75_SUBKEY L"Software\\CoreMonitor\\Core75"
#define REG_COREM1_SUBKEY L"Software\\CoreMonitor\\CoreM1"
#define RUN_SUBKEY        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALNAME       L"CoreMonitor"

AppSettings g_Settings = {
    .startWithWindows = TRUE,
#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
    .core75 = {
        .centerStyle = ICON_CENTER_PERCENT,
        .lowBatteryAlert = FALSE,
        .lowAlertThreshold = 20,
        .longevityAlert = FALSE,
        .longevityAlertThreshold = 85,
        .highBatteryAlert = FALSE,
        .highAlertThreshold = 100,
        .capsLockIndicator = FALSE,
        .lowAlertShownForDischarge = FALSE,
        .longevityAlertShownForCharge = FALSE,
        .highAlertShownForCharge = FALSE
    },
#endif
#if defined(BUILD_TARGET_COREM1) || (!defined(BUILD_TARGET_CORE75))
    .corem1 = {
        .centerStyle = ICON_CENTER_PERCENT,
        .lowBatteryAlert = FALSE,
        .lowAlertThreshold = 20,
        .longevityAlert = FALSE,
        .longevityAlertThreshold = 85,
        .highBatteryAlert = FALSE,
        .highAlertThreshold = 100,
        .capsLockIndicator = FALSE,
        .lowAlertShownForDischarge = FALSE,
        .longevityAlertShownForCharge = FALSE,
        .highAlertShownForCharge = FALSE
    }
#endif
};

static void LoadDeviceSettings(DeviceSettings *ds, const WCHAR *subKey, const WCHAR *fallbackKey) {
    HKEY hKey;
    const WCHAR *keysToTry[2] = { subKey, fallbackKey };

    for (int k = 0; k < 2; k++) {
        if (!keysToTry[k] || keysToTry[k][0] == L'\0') continue;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, keysToTry[k], 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD val = 0, size = sizeof(val);
            BOOL loadedAny = FALSE;

            if (RegQueryValueExW(hKey, L"CenterStyle", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                if (val > 2) val = 1;
                ds->centerStyle = (CenterIconStyle)val;
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"LowBatteryAlert", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                ds->lowBatteryAlert = (val != 0);
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"LowAlertThreshold", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                if (val == 10 || val == 15 || val == 20) ds->lowAlertThreshold = (int)val;
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"LongevityAlert", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                ds->longevityAlert = (val != 0);
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"LongevityAlertThreshold", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                if (val == 80 || val == 85) ds->longevityAlertThreshold = (int)val;
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"HighBatteryAlert", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                ds->highBatteryAlert = (val != 0);
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"HighAlertThreshold", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                if (val == 95 || val == 100) ds->highAlertThreshold = (int)val;
                loadedAny = TRUE;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"CapsLockIndicator", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                ds->capsLockIndicator = (val != 0);
                loadedAny = TRUE;
            }
            RegCloseKey(hKey);
            if (loadedAny) break;
        }
    }
}

static void SaveDeviceSettings(const DeviceSettings *ds, const WCHAR *subKey) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = (DWORD)ds->centerStyle;
        RegSetValueExW(hKey, L"CenterStyle", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = ds->lowBatteryAlert ? 1 : 0;
        RegSetValueExW(hKey, L"LowBatteryAlert", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)ds->lowAlertThreshold;
        RegSetValueExW(hKey, L"LowAlertThreshold", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = ds->longevityAlert ? 1 : 0;
        RegSetValueExW(hKey, L"LongevityAlert", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)ds->longevityAlertThreshold;
        RegSetValueExW(hKey, L"LongevityAlertThreshold", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = ds->highBatteryAlert ? 1 : 0;
        RegSetValueExW(hKey, L"HighBatteryAlert", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)ds->highAlertThreshold;
        RegSetValueExW(hKey, L"HighAlertThreshold", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = ds->capsLockIndicator ? 1 : 0;
        RegSetValueExW(hKey, L"CapsLockIndicator", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

void Settings_Load(void) {
    HKEY hKey;
    BOOL isFirstRun = TRUE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_ROOT_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        isFirstRun = FALSE;
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(hKey, L"StartWithWindows", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            g_Settings.startWithWindows = (val != 0);
        }
        RegCloseKey(hKey);
    }

#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
    // Load Core75 settings (fallback to root key for backwards compatibility)
    LoadDeviceSettings(&g_Settings.core75, REG_CORE75_SUBKEY, REG_ROOT_SUBKEY);
#endif

#if defined(BUILD_TARGET_COREM1) || (!defined(BUILD_TARGET_CORE75))
    // Load CoreM1 settings
    LoadDeviceSettings(&g_Settings.corem1, REG_COREM1_SUBKEY, NULL);
#endif

    if (isFirstRun) {
        g_Settings.startWithWindows = TRUE;
        Settings_SetAutostart(TRUE);
        Settings_Save();
    } else {
        BOOL currentlyInRun = Settings_GetAutostart();
        if (g_Settings.startWithWindows && !currentlyInRun) {
            Settings_SetAutostart(TRUE);
        } else if (!g_Settings.startWithWindows && currentlyInRun) {
            Settings_SetAutostart(FALSE);
        }
    }
}

void Settings_Save(void) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_ROOT_SUBKEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = g_Settings.startWithWindows ? 1 : 0;
        RegSetValueExW(hKey, L"StartWithWindows", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }

#if defined(BUILD_TARGET_CORE75) || (!defined(BUILD_TARGET_COREM1))
    // Save device-specific settings
    SaveDeviceSettings(&g_Settings.core75, REG_CORE75_SUBKEY);
    // Also save Core75 to root for backwards compatibility with older builds
    SaveDeviceSettings(&g_Settings.core75, REG_ROOT_SUBKEY);
#endif

#if defined(BUILD_TARGET_COREM1) || (!defined(BUILD_TARGET_CORE75))
    SaveDeviceSettings(&g_Settings.corem1, REG_COREM1_SUBKEY);
#endif
}

BOOL Settings_GetAutostart(void) {
    HKEY hKey;
    BOOL enabled = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR path[MAX_PATH];
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, RUN_VALNAME, NULL, NULL, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            enabled = TRUE;
        }
        RegCloseKey(hKey);
    }
    return enabled;
}

void Settings_SetAutostart(BOOL enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_SUBKEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            WCHAR quotedPath[MAX_PATH + 4];
            _snwprintf(quotedPath, sizeof(quotedPath) / sizeof(WCHAR), L"\"%s\"", exePath);
            quotedPath[(sizeof(quotedPath) / sizeof(WCHAR)) - 1] = L'\0';
            RegSetValueExW(hKey, RUN_VALNAME, 0, REG_SZ, (const BYTE*)quotedPath, (wcslen(quotedPath) + 1) * sizeof(WCHAR));
        } else {
            RegDeleteValueW(hKey, RUN_VALNAME);
        }
        RegCloseKey(hKey);
    }
    g_Settings.startWithWindows = enable;
    Settings_Save();
}
