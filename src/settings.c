#include "settings.h"

#define REG_SUBKEY L"Software\\CoreMonitor"
#define RUN_SUBKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALNAME L"CoreMonitor"

AppSettings g_Settings = {
    .centerStyle = ICON_CENTER_KEYBOARD,
    .startWithWindows = TRUE,
    .lowBatteryAlert = TRUE,
    .highBatteryAlert = TRUE,
    .lowAlertShownForDischarge = FALSE,
    .highAlertShownForCharge = FALSE
};

void Settings_Load(void) {
    HKEY hKey;
    BOOL isFirstRun = TRUE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        isFirstRun = FALSE;
        DWORD val = 0, size = sizeof(val);

        if (RegQueryValueExW(hKey, L"CenterStyle", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            g_Settings.centerStyle = (val != 0) ? ICON_CENTER_PERCENT : ICON_CENTER_KEYBOARD;
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LowBatteryAlert", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            g_Settings.lowBatteryAlert = (val != 0);
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"HighBatteryAlert", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            g_Settings.highBatteryAlert = (val != 0);
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"StartWithWindows", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            g_Settings.startWithWindows = (val != 0);
        }
        RegCloseKey(hKey);
    }

    if (isFirstRun) {
        // First time running the app: enable autostart by default
        g_Settings.startWithWindows = TRUE;
        Settings_SetAutostart(TRUE);
        Settings_Save();
    } else {
        // Ensure registry Run key matches user preference
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
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = (DWORD)g_Settings.centerStyle;
        RegSetValueExW(hKey, L"CenterStyle", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_Settings.lowBatteryAlert ? 1 : 0;
        RegSetValueExW(hKey, L"LowBatteryAlert", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_Settings.highBatteryAlert ? 1 : 0;
        RegSetValueExW(hKey, L"HighBatteryAlert", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegDeleteValueW(hKey, L"ChargingOverride");
        val = g_Settings.startWithWindows ? 1 : 0;
        RegSetValueExW(hKey, L"StartWithWindows", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
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
            wsprintfW(quotedPath, L"\"%s\"", exePath);
            RegSetValueExW(hKey, RUN_VALNAME, 0, REG_SZ, (const BYTE*)quotedPath, (wcslen(quotedPath) + 1) * sizeof(WCHAR));
        } else {
            RegDeleteValueW(hKey, RUN_VALNAME);
        }
        RegCloseKey(hKey);
    }
    g_Settings.startWithWindows = enable;
    Settings_Save();
}
