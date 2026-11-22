#define _WIN32_WINNT 0x0601
#define _WIN32_IE    0x0600
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <dbt.h>
#include <stdio.h>

#include "common.h"
#include "hid_common.h"
#include "battery_stats.h"
#include "icon_generator.h"
#include "settings.h"

#if defined(BUILD_TARGET_CORE75)
#include "device_core75.h"
#endif

#if defined(BUILD_TARGET_COREM1)
#include "device_corem1.h"
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#define WM_TRAY_CALLBACK (WM_APP + 100)
#define IDT_POLL_TIMER_CORE75 1001
#define IDT_POLL_TIMER_COREM1 1002
#define IDT_CLICK_TIMER_CORE75 1003
#define IDT_CLICK_TIMER_COREM1 1004
#define IDT_ANIM_TIMER        1005

#define TOAST_MANUAL_COOLDOWN_MS 4000

// Command IDs
#define IDM_STATUS_TITLE  2001
#define IDM_STATUS_INFO   2002
#define IDM_STATUS_EST    2003
#define IDM_REFRESH       2004
#define IDM_DETAILS       2005

#define IDM_STYLE_RING_ONLY 2010
#define IDM_STYLE_PERCENT   2011
#define IDM_STYLE_GLYPH     2012

#define IDM_LOW_ENABLE      2020
#define IDM_LOW_10          2021
#define IDM_LOW_15          2022
#define IDM_LOW_20          2023

#define IDM_LONG_ENABLE     2030
#define IDM_LONG_80         2031
#define IDM_LONG_85         2032

#define IDM_FULL_ENABLE     2040
#define IDM_FULL_95         2041
#define IDM_FULL_100        2042

#define IDM_CAPSLOCK_INDICATOR 2050

#define IDM_AUTOSTART       2060
#define IDM_EXIT            2062

static const WCHAR *CLASS_NAME = L"CoreMonitor_Class";
static const WCHAR *DETAILS_CLASS_NAME = L"CoreMonitor_DetailsClass";

#if defined(BUILD_TARGET_CORE75) && !defined(BUILD_TARGET_COREM1)
static const WCHAR *MUTEX_NAME = L"Global\\CoreMonitor_Core75_Mutex";
#elif defined(BUILD_TARGET_COREM1) && !defined(BUILD_TARGET_CORE75)
static const WCHAR *MUTEX_NAME = L"Global\\CoreMonitor_CoreM1_Mutex";
#else
static const WCHAR *MUTEX_NAME = L"Global\\CoreMonitor_Mutex";
#endif

static HWND s_hWnd = NULL;
static HWND s_hDetailsWnd = NULL;
static BOOL s_detailsClassRegistered = FALSE;
static RECT s_closeButtonRect = {0};
static BOOL s_closeHover = FALSE;
static DeviceModel s_detailsTargetModel = MODEL_CORE75;

static UINT s_uTaskbarRestartMsg = 0;
static float s_breathPhase = 0.0f;
static BOOL s_isAnimating = FALSE;
static HDEVNOTIFY s_hDevNotify = NULL;
static BOOL s_isResumingFromSuspend = FALSE;

#if defined(BUILD_TARGET_CORE75)
static NOTIFYICONDATAW s_nidCore75 = {0};
static HICON s_hCurrentIconCore75 = NULL;
static DeviceStatus s_statusCore75 = {0};
static BatteryStatsContext s_statsCore75 = {0};
static DWORD s_lastCore75InputTick = 0;
static DWORD s_lastCore75RadioQueryTick = 0;
static DWORD s_lastCore75ManualToastTick = 0;
#endif

#if defined(BUILD_TARGET_COREM1)
static NOTIFYICONDATAW s_nidCoreM1 = {0};
static HICON s_hCurrentIconCoreM1 = NULL;
static DeviceStatus s_statusCoreM1 = {0};
static BatteryStatsContext s_statsCoreM1 = {0};
static DWORD s_lastCoreM1InputTick = 0;
static DWORD s_lastCoreM1RadioQueryTick = 0;
static DWORD s_lastCoreM1ManualToastTick = 0;
#endif

typedef enum {
    APPMODE_DEFAULT = 0,
    APPMODE_ALLOWDARK = 1,
    APPMODE_FORCEDARK = 2,
    APPMODE_FORCELIGHT = 3,
    APPMODE_MAX = 4
} PreferredAppMode;

typedef PreferredAppMode (WINAPI *pfnSetPreferredAppMode_t)(PreferredAppMode);
typedef BOOL (WINAPI *pfnAllowDarkModeForWindow_t)(HWND, BOOL);
typedef void (WINAPI *pfnFlushMenuThemes_t)(void);
typedef BOOL (WINAPI *pfnShouldAppsUseDarkMode_t)(void);
typedef HRESULT (WINAPI *pfnDwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
typedef UINT (WINAPI *pfnGetDpiForWindow_t)(HWND);
typedef HRESULT (WINAPI *pfnSetCurrentProcessExplicitAppUserModelID_t)(PCWSTR);

static pfnSetPreferredAppMode_t s_pfnSetPreferredAppMode = NULL;
static pfnAllowDarkModeForWindow_t s_pfnAllowDarkModeForWindow = NULL;
static pfnFlushMenuThemes_t s_pfnFlushMenuThemes = NULL;
static pfnShouldAppsUseDarkMode_t s_pfnShouldAppsUseDarkMode = NULL;
static pfnDwmSetWindowAttribute_t s_pfnDwmSetWindowAttribute = NULL;
static pfnGetDpiForWindow_t s_pfnGetDpiForWindow = NULL;
static pfnSetCurrentProcessExplicitAppUserModelID_t s_pfnSetCurrentProcessExplicitAppUserModelID = NULL;

static void InitDarkMode(void) {
    HMODULE hUxTheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxTheme) {
        s_pfnSetPreferredAppMode = (pfnSetPreferredAppMode_t)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
        s_pfnFlushMenuThemes = (pfnFlushMenuThemes_t)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));
        s_pfnAllowDarkModeForWindow = (pfnAllowDarkModeForWindow_t)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
        s_pfnShouldAppsUseDarkMode = (pfnShouldAppsUseDarkMode_t)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(132));

        if (s_pfnSetPreferredAppMode) s_pfnSetPreferredAppMode(APPMODE_ALLOWDARK);
        if (s_pfnFlushMenuThemes) s_pfnFlushMenuThemes();
    }

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        s_pfnGetDpiForWindow = (pfnGetDpiForWindow_t)GetProcAddress(hUser32, "GetDpiForWindow");
    }

    HMODULE hShell = LoadLibraryW(L"shell32.dll");
    if (hShell) {
        s_pfnSetCurrentProcessExplicitAppUserModelID = (pfnSetCurrentProcessExplicitAppUserModelID_t)GetProcAddress(hShell, "SetCurrentProcessExplicitAppUserModelID");
    }

    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        s_pfnDwmSetWindowAttribute = (pfnDwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
    }
}

static BOOL IsAppInDarkMode(void) {
    if (s_pfnShouldAppsUseDarkMode) return s_pfnShouldAppsUseDarkMode();
    HKEY hKey;
    DWORD val = 0, size = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        RegCloseKey(hKey);
    }
    return FALSE;
}

static void ApplyWindowTheme(HWND hWnd, BOOL isDark) {
    if (s_pfnAllowDarkModeForWindow) s_pfnAllowDarkModeForWindow(hWnd, isDark);
    if (s_pfnDwmSetWindowAttribute) {
        BOOL dwmDark = isDark ? TRUE : FALSE;
        s_pfnDwmSetWindowAttribute(hWnd, 20, &dwmDark, sizeof(dwmDark));
        s_pfnDwmSetWindowAttribute(hWnd, 19, &dwmDark, sizeof(dwmDark));
    }
}

#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
#ifndef WTS_SESSION_LOCK
#define WTS_SESSION_LOCK 0x7
#endif
#ifndef WTS_SESSION_UNLOCK
#define WTS_SESSION_UNLOCK 0x8
#endif
#ifndef NOTIFY_FOR_THIS_SESSION
#define NOTIFY_FOR_THIS_SESSION 0
#endif
#ifndef PBT_POWERSETTINGCHANGE
#define PBT_POWERSETTINGCHANGE 0x8013
#endif

typedef HANDLE HPOWERNOTIFY;
typedef struct {
    GUID PowerSetting;
    DWORD DataLength;
    UCHAR Data[1];
} POWERBROADCAST_SETTING_M;

static const GUID GUID_CONSOLE_DISPLAY_STATE_M = 
    { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };

typedef BOOL (WINAPI *pfnWTSRegisterSessionNotification_t)(HWND, DWORD);
typedef BOOL (WINAPI *pfnWTSUnRegisterSessionNotification_t)(HWND);
typedef HPOWERNOTIFY (WINAPI *pfnRegisterPowerSettingNotification_t)(HANDLE, LPCGUID, DWORD);
typedef BOOL (WINAPI *pfnUnregisterPowerSettingNotification_t)(HPOWERNOTIFY);

static pfnWTSRegisterSessionNotification_t s_pfnWTSRegisterSessionNotification = NULL;
static pfnWTSUnRegisterSessionNotification_t s_pfnWTSUnRegisterSessionNotification = NULL;
static pfnRegisterPowerSettingNotification_t s_pfnRegisterPowerSettingNotification = NULL;
static pfnUnregisterPowerSettingNotification_t s_pfnUnregisterPowerSettingNotification = NULL;
static HPOWERNOTIFY s_hPowerNotify = NULL;
static BOOL s_isWtsRegistered = FALSE;
static BOOL s_isDisplayOff = FALSE;
static BOOL s_isSessionLocked = FALSE;

static void InitPowerAndSessionHooks(void) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        s_pfnRegisterPowerSettingNotification = (pfnRegisterPowerSettingNotification_t)GetProcAddress(hUser, "RegisterPowerSettingNotification");
        s_pfnUnregisterPowerSettingNotification = (pfnUnregisterPowerSettingNotification_t)GetProcAddress(hUser, "UnregisterPowerSettingNotification");
    }
    HMODULE hWts = LoadLibraryW(L"wtsapi32.dll");
    if (hWts) {
        s_pfnWTSRegisterSessionNotification = (pfnWTSRegisterSessionNotification_t)GetProcAddress(hWts, "WTSRegisterSessionNotification");
        s_pfnWTSUnRegisterSessionNotification = (pfnWTSUnRegisterSessionNotification_t)GetProcAddress(hWts, "WTSUnRegisterSessionNotification");
    }
}

static void FormatDeviceTooltip(DeviceModel model, const DeviceStatus *st, const BatteryStatsContext *stats, WCHAR *outTip, DWORD maxLen) {
    if (!outTip || maxLen == 0 || !st || !stats) return;
    WCHAR estBuf[64];
    int effPercent = (st->batteryPercent >= 0) ? st->batteryPercent : st->lastKnownPercent;
    const WCHAR *modelName = (model == MODEL_COREM1) ? L"URX Core M1" : L"URX Core75";

    BatteryStats_FormatRemainingText(
        stats,
        effPercent,
        st->isCharging,
        st->isSleeping,
        (st->mode == MODE_DISCONNECTED),
        estBuf, 64
    );

    if (st->mode == MODE_DISCONNECTED) {
        _snwprintf(outTip, maxLen, L"%s: Disconnected", modelName);
    } else if (st->isSleeping) {
        if (effPercent < 0) {
            _snwprintf(outTip, maxLen, L"%s: Asleep (Wake device) - [2.4GHz]\nOffline", modelName);
        } else {
            _snwprintf(outTip, maxLen, L"%s: %d%% - [Asleep - 2.4GHz]\n%s", modelName, effPercent, estBuf);
        }
    } else if (effPercent < 0) {
        _snwprintf(outTip, maxLen, L"%s: Connecting... - [%s]\n%s", modelName, st->modeName, estBuf);
    } else if (st->isCharging) {
        _snwprintf(outTip, maxLen, L"%s: %d%% - [%s - Charging]\n%s", modelName, effPercent, st->modeName, estBuf);
    } else {
        _snwprintf(outTip, maxLen, L"%s: %d%% - [%s]\n%s", modelName, effPercent, st->modeName, estBuf);
    }
    outTip[maxLen - 1] = L'\0';
}

static void ShowDeviceToast(NOTIFYICONDATAW *nid, const WCHAR *title, const WCHAR *msg, DWORD flags) {
    if (!nid) return;
    nid->uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
    wcsncpy(nid->szInfoTitle, title, 63);
    nid->szInfoTitle[63] = L'\0';
    wcsncpy(nid->szInfo, msg, 255);
    nid->szInfo[255] = L'\0';
    nid->dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, nid);
}

static void ClearDeviceToast(NOTIFYICONDATAW *nid) {
    if (!nid) return;
    nid->uFlags = NIF_INFO;
    nid->szInfo[0] = L'\0';
    nid->szInfoTitle[0] = L'\0';
    nid->dwInfoFlags = 0;
    Shell_NotifyIconW(NIM_MODIFY, nid);
}

static void CheckDeviceAlerts(DeviceModel model, DeviceSettings *ds, NOTIFYICONDATAW *nid, int percent, BOOL isCharging, BOOL isDisc) {
    if (!ds || !nid) return;
    const WCHAR *modelName = (model == MODEL_COREM1) ? L"URX Core M1" : L"URX Core75";
    WCHAR toastBuf[256];
    WCHAR titleBuf[64];

    // Low Battery
    if (ds->lowBatteryAlert && percent > 0 && percent <= ds->lowAlertThreshold && !isCharging && !isDisc) {
        if (!ds->lowAlertShownForDischarge) {
            _snwprintf(titleBuf, 64, L"%s - Low Battery Alert", modelName);
            _snwprintf(toastBuf, 255, L"Battery is down to %d%%. Connect the USB cable to charge.", percent);
            ShowDeviceToast(nid, titleBuf, toastBuf, NIIF_WARNING);
            ds->lowAlertShownForDischarge = TRUE;
        }
    } else if (percent > (ds->lowAlertThreshold + 5) || isCharging) {
        ds->lowAlertShownForDischarge = FALSE;
    }

    // Longevity
    if (ds->longevityAlert && percent >= ds->longevityAlertThreshold &&
        (percent < ds->highAlertThreshold || !ds->highBatteryAlert) && isCharging && !isDisc) {
        if (!ds->longevityAlertShownForCharge) {
            _snwprintf(titleBuf, 64, L"%s - Battery Longevity Alert", modelName);
            _snwprintf(toastBuf, 255, L"Battery reached %d%%. Disconnect charger to protect battery.", percent);
            ShowDeviceToast(nid, titleBuf, toastBuf, NIIF_INFO);
            ds->longevityAlertShownForCharge = TRUE;
        }
    } else if (percent < (ds->longevityAlertThreshold - 5) || !isCharging) {
        ds->longevityAlertShownForCharge = FALSE;
    }

    // High / Full
    if (ds->highBatteryAlert && percent >= ds->highAlertThreshold && isCharging && !isDisc) {
        if (!ds->highAlertShownForCharge) {
            _snwprintf(titleBuf, 64, L"%s - High Battery Alert", modelName);
            _snwprintf(toastBuf, 255, L"Battery reached %d%%. Fully charged and ready for wireless use.", percent);
            ShowDeviceToast(nid, titleBuf, toastBuf, NIIF_INFO);
            ds->highAlertShownForCharge = TRUE;
        }
    } else if (percent < (ds->highAlertThreshold - 5) || !isCharging) {
        ds->highAlertShownForCharge = FALSE;
    }
}

static void UpdateTrayIcon(DeviceModel model) {
    NOTIFYICONDATAW *nid = NULL;
    HICON *hCurrentIcon = NULL;
    DeviceStatus *st = NULL;
    BatteryStatsContext *stats = NULL;
    CenterIconStyle style = ICON_CENTER_PERCENT;

#if defined(BUILD_TARGET_CORE75)
    if (model == MODEL_CORE75) {
        nid = &s_nidCore75;
        hCurrentIcon = &s_hCurrentIconCore75;
        st = &s_statusCore75;
        stats = &s_statsCore75;
        style = g_Settings.core75.centerStyle;
    }
#endif
#if defined(BUILD_TARGET_COREM1)
    if (model == MODEL_COREM1) {
        nid = &s_nidCoreM1;
        hCurrentIcon = &s_hCurrentIconCoreM1;
        st = &s_statusCoreM1;
        stats = &s_statsCoreM1;
        style = g_Settings.corem1.centerStyle;
    }
#endif

    if (!nid || !st || !stats || !hCurrentIcon) return;

    BOOL lightTaskbar = IconGen_IsTaskbarLight();
    BOOL isDisconnected = (st->mode == MODE_DISCONNECTED);
    int effPercent = (st->batteryPercent >= 0) ? st->batteryPercent : st->lastKnownPercent;

    BOOL isCapsLockOn = FALSE;
#if defined(BUILD_TARGET_CORE75)
    if (model == MODEL_CORE75 && g_Settings.core75.capsLockIndicator) {
        isCapsLockOn = st->isCapsLock;
    }
#endif

    HICON hNewIcon = IconGen_CreateBatteryIcon(
        effPercent,
        st->isCharging,
        s_breathPhase,
        st->isSleeping,
        isDisconnected,
        style,
        model,
        lightTaskbar,
        isCapsLockOn
    );

    if (hNewIcon) {
        nid->uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        nid->hIcon = hNewIcon;
        FormatDeviceTooltip(model, st, stats, nid->szTip, 128);
        nid->szTip[127] = L'\0';

        Shell_NotifyIconW(NIM_MODIFY, nid);

        if (*hCurrentIcon) DestroyIcon(*hCurrentIcon);
        *hCurrentIcon = hNewIcon;

        if (s_hDetailsWnd && IsWindow(s_hDetailsWnd) && s_detailsTargetModel == model) {
            SendMessageW(s_hDetailsWnd, WM_SETICON, ICON_BIG, (LPARAM)*hCurrentIcon);
            SendMessageW(s_hDetailsWnd, WM_SETICON, ICON_SMALL, (LPARAM)*hCurrentIcon);
        }
    }
}

static void UpdateAllTrayIcons(void) {
#if defined(BUILD_TARGET_CORE75)
    UpdateTrayIcon(MODEL_CORE75);
#endif
#if defined(BUILD_TARGET_COREM1)
    UpdateTrayIcon(MODEL_COREM1);
#endif
}

static void SyncAnimationTimer(void) {
    BOOL anyCharging = FALSE;
#if defined(BUILD_TARGET_CORE75)
    if (s_statusCore75.isCharging) anyCharging = TRUE;
#endif
#if defined(BUILD_TARGET_COREM1)
    if (s_statusCoreM1.isCharging) anyCharging = TRUE;
#endif

    if (anyCharging) {
        if (!s_isAnimating) {
            s_isAnimating = TRUE;
            SetTimer(s_hWnd, IDT_ANIM_TIMER, 65, NULL); // ~15.4 FPS
        }
    } else {
        if (s_isAnimating) {
            s_isAnimating = FALSE;
            KillTimer(s_hWnd, IDT_ANIM_TIMER);
            s_breathPhase = 0.0f;
            UpdateAllTrayIcons();
        }
    }
}

// -----------------------------------------------------------------------------
// Details & Telemetry Dialog
// -----------------------------------------------------------------------------
static void DrawCardPanel(HDC hdc, int x, int y, int w, int h, COLORREF bgCol, COLORREF borderCol, int radius) {
    HBRUSH hBr = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    RoundRect(hdc, x, y, x + w, y + h, radius, radius);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);
}

static void DrawCardRow(HDC hdc, int left, int top, int w, int rowH,
                        const WCHAR *label, const WCHAR *val,
                        COLORREF colLabel, COLORREF colVal, HFONT hFont) {
    SelectObject(hdc, hFont);
    SetTextColor(hdc, colLabel);
    RECT rLabel = { left, top, left + (w / 2), top + rowH };
    DrawTextW(hdc, label, -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SetTextColor(hdc, colVal);
    RECT rVal = { left + (w / 3), top, left + w, top + rowH };
    DrawTextW(hdc, val, -1, &rVal, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static HFONT s_hFontTitle = NULL;
static HFONT s_hFontSub = NULL;
static HFONT s_hFontSecHdr = NULL;
static HFONT s_hFontRow = NULL;
static HFONT s_hFontBadge = NULL;
static HFONT s_hFontBtn = NULL;
static int s_detailsDpi = 0;

static void CleanupDetailsFonts(void) {
    if (s_hFontTitle)  { DeleteObject(s_hFontTitle);  s_hFontTitle = NULL; }
    if (s_hFontSub)    { DeleteObject(s_hFontSub);    s_hFontSub = NULL; }
    if (s_hFontSecHdr) { DeleteObject(s_hFontSecHdr); s_hFontSecHdr = NULL; }
    s_hFontRow = NULL; // Aliased to s_hFontSub, avoid double-free
    if (s_hFontBadge)  { DeleteObject(s_hFontBadge);  s_hFontBadge = NULL; }
    if (s_hFontBtn)    { DeleteObject(s_hFontBtn);    s_hFontBtn = NULL; }
    s_detailsDpi = 0;
}

static void EnsureDetailsFonts(int dpi) {
    if (dpi <= 0) dpi = 96;
    if (s_detailsDpi == dpi && s_hFontTitle) return;

    CleanupDetailsFonts();
    s_detailsDpi = dpi;

    #define SC_FNT(val) MulDiv((val), dpi, 96)
    s_hFontTitle  = CreateFontW(-SC_FNT(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    s_hFontSub    = CreateFontW(-SC_FNT(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    s_hFontSecHdr = CreateFontW(-SC_FNT(10), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    s_hFontRow    = s_hFontSub; // Reuse identical 11pt Segoe UI regular font
    s_hFontBadge  = CreateFontW(-SC_FNT(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    s_hFontBtn    = CreateFontW(-SC_FNT(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    #undef SC_FNT
}

static LRESULT CALLBACK DetailsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            BOOL isDark = IsAppInDarkMode();
            ApplyWindowTheme(hWnd, isDark);
            break;
        }

        case WM_SETTINGCHANGE: {
            BOOL isDark = IsAppInDarkMode();
            ApplyWindowTheme(hWnd, isDark);
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        }

        case WM_DPICHANGED: {
            RECT *r = (RECT*)lParam;
            SetWindowPos(hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right;
            int height = rc.bottom;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            BOOL isDark = IsAppInDarkMode();

            COLORREF colBg       = isDark ? RGB(24, 24, 27)    : RGB(246, 247, 249);
            COLORREF colCardBg   = isDark ? RGB(34, 34, 39)    : RGB(255, 255, 255);
            COLORREF colBorder   = isDark ? RGB(52, 52, 60)    : RGB(226, 232, 240);
            COLORREF colTextPri  = isDark ? RGB(244, 244, 245) : RGB(15, 23, 42);
            COLORREF colTextSec  = isDark ? RGB(161, 161, 170) : RGB(100, 116, 139);
            COLORREF colHeader   = isDark ? RGB(140, 140, 150) : RGB(100, 116, 139);
            COLORREF colAccent   = isDark ? RGB(52, 211, 153)  : RGB(5, 150, 105);
            COLORREF colMuted    = isDark ? RGB(113, 113, 122) : RGB(148, 163, 184);

            COLORREF colBtnBg    = s_closeHover
                                   ? (isDark ? RGB(58, 58, 66) : RGB(226, 232, 240))
                                   : (isDark ? RGB(40, 40, 46) : RGB(238, 242, 246));
            COLORREF colBtnBdr   = isDark ? RGB(70, 70, 78)    : RGB(203, 213, 225);
            COLORREF colBtnText  = isDark ? RGB(244, 244, 245) : RGB(15, 23, 42);

            HBRUSH hBrBg = CreateSolidBrush(colBg);
            FillRect(memDC, &rc, hBrBg);
            DeleteObject(hBrBg);

            int dpi = 96;
            if (s_pfnGetDpiForWindow) dpi = s_pfnGetDpiForWindow(hWnd);
            if (dpi <= 0) dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            if (dpi <= 0) dpi = 96;
            EnsureDetailsFonts(dpi);
            #define SC(val) MulDiv((val), dpi, 96)

            SetBkMode(memDC, TRANSPARENT);

            int pad = SC(16);
            int cardW = width - (pad * 2);
            int curY = SC(14);

            const DeviceStatus *st = NULL;
            const BatteryStatsContext *stats = NULL;
            const DeviceSettings *ds = NULL;
            const WCHAR *titleStr = L"URX Peripheral";
            const WCHAR *subTitleStr = L"Device Telemetry \x2022 v2.1.0";
            BOOL isFiltering = FALSE;

#if defined(BUILD_TARGET_CORE75)
            if (s_detailsTargetModel == MODEL_CORE75) {
                st = &s_statusCore75;
                stats = &s_statsCore75;
                ds = &g_Settings.core75;
                titleStr = L"URX Core75";
                subTitleStr = L"Mechanical Keyboard Telemetry \x2022 v2.1.0";
                isFiltering = DeviceCore75_IsFilteringTransient();
            }
#endif
#if defined(BUILD_TARGET_COREM1)
            if (s_detailsTargetModel == MODEL_COREM1) {
                st = &s_statusCoreM1;
                stats = &s_statsCoreM1;
                ds = &g_Settings.corem1;
                titleStr = L"URX Core M1";
                subTitleStr = L"Wireless Gaming Mouse Telemetry \x2022 v2.1.0";
                isFiltering = DeviceCoreM1_IsFilteringTransient();
            }
#endif

            // Title & Subtitle
            SelectObject(memDC, s_hFontTitle);
            SetTextColor(memDC, colTextPri);
            TextOutW(memDC, pad, curY, titleStr, wcslen(titleStr));

            SelectObject(memDC, s_hFontSub);
            SetTextColor(memDC, colTextSec);
            TextOutW(memDC, pad, curY + SC(20), subTitleStr, wcslen(subTitleStr));

            // Status Badge
            int effPercent = (st && st->batteryPercent >= 0) ? st->batteryPercent : (st ? st->lastKnownPercent : -1);
            WCHAR badgeText[32];
            COLORREF colBadgeBg, colBadgeBdr, colBadgeText;
            if (!st || st->mode == MODE_DISCONNECTED) {
                wcscpy(badgeText, L"Offline");
                colBadgeBg = isDark ? RGB(50, 25, 25) : RGB(254, 226, 226);
                colBadgeBdr = isDark ? RGB(100, 45, 45) : RGB(248, 113, 113);
                colBadgeText = isDark ? RGB(248, 113, 113) : RGB(185, 28, 28);
            } else if (st->isSleeping) {
                if (effPercent < 0) {
                    wcscpy(badgeText, L"Asleep");
                } else {
                    _snwprintf(badgeText, 32, L"%d%% - Asleep", effPercent);
                }
                colBadgeBg = isDark ? RGB(20, 45, 65) : RGB(224, 242, 254);
                colBadgeBdr = isDark ? RGB(40, 90, 130) : RGB(56, 189, 248);
                colBadgeText = isDark ? RGB(56, 189, 248) : RGB(3, 105, 161);
            } else if (effPercent < 0) {
                wcscpy(badgeText, L"Connecting...");
                colBadgeBg = isDark ? RGB(35, 35, 45) : RGB(240, 240, 245);
                colBadgeBdr = isDark ? RGB(70, 70, 85) : RGB(200, 200, 210);
                colBadgeText = isDark ? RGB(180, 180, 200) : RGB(100, 100, 120);
            } else if (st->isCharging) {
                _snwprintf(badgeText, 32, L"%d%% - Charging", effPercent);
                colBadgeBg = isDark ? RGB(6, 60, 45) : RGB(209, 250, 229);
                colBadgeBdr = isDark ? RGB(16, 185, 129) : RGB(16, 185, 129);
                colBadgeText = isDark ? RGB(52, 211, 153) : RGB(4, 120, 87);
            } else {
                _snwprintf(badgeText, 32, L"%d%% - Active", effPercent);
                colBadgeBg = isDark ? RGB(6, 60, 45) : RGB(209, 250, 229);
                colBadgeBdr = isDark ? RGB(16, 185, 129) : RGB(16, 185, 129);
                colBadgeText = isDark ? RGB(52, 211, 153) : RGB(4, 120, 87);
            }

            SelectObject(memDC, s_hFontBadge);
            SIZE badgeSize;
            GetTextExtentPoint32W(memDC, badgeText, wcslen(badgeText), &badgeSize);
            int badgeW = badgeSize.cx + SC(16);
            int badgeH = badgeSize.cy + SC(6);
            int badgeX = width - pad - badgeW;
            int badgeY = curY + SC(2);

            DrawCardPanel(memDC, badgeX, badgeY, badgeW, badgeH, colBadgeBg, colBadgeBdr, SC(8));
            SetTextColor(memDC, colBadgeText);
            RECT rBadge = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
            DrawTextW(memDC, badgeText, -1, &rBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            curY += SC(44);

            WCHAR estBuf[64];
            if (stats && st) {
                BatteryStats_FormatRemainingText(
                    stats,
                    effPercent,
                    st->isCharging,
                    st->isSleeping,
                    (st->mode == MODE_DISCONNECTED),
                    estBuf, 64
                );
            } else {
                wcscpy(estBuf, L"Offline");
            }

            double dRate = stats ? BatteryStats_GetDischargeRatePerHour(stats) : 0.0;
            double cRate = stats ? BatteryStats_GetChargeRatePerHour(stats) : 0.0;

            double endHours = 0.0;
            WCHAR enduranceStr[64];
            if (stats && BatteryStats_GetEstimatedEnduranceHours(stats, &endHours)) {
                if (endHours >= 24.0) {
                    double days = endHours / 24.0;
                    _snwprintf(enduranceStr, 64, L"~%.0f hrs (~%.1f days)", endHours, days);
                } else {
                    _snwprintf(enduranceStr, 64, L"~%.1f hrs", endHours);
                }
            } else {
                wcscpy(enduranceStr, L"Calculating...");
            }

            WCHAR dRateStr[32], cRateStr[32];
            if (dRate < 1.0) {
                _snwprintf(dRateStr, 32, L"~%.2f%% / hour", dRate);
            } else {
                _snwprintf(dRateStr, 32, L"~%.1f%% / hour", dRate);
            }
            _snwprintf(cRateStr, 32, L"~%.1f%% / hour", cRate);

            // Card 1: BATTERY & RUNTIME
            int card1H = (s_detailsTargetModel == MODEL_COREM1) ? SC(111) : SC(92);
            DrawCardPanel(memDC, pad, curY, cardW, card1H, colCardBg, colBorder, SC(8));
            SelectObject(memDC, s_hFontSecHdr);
            SetTextColor(memDC, colHeader);
            TextOutW(memDC, pad + SC(12), curY + SC(8), L"BATTERY & RUNTIME", 17);

            int innerY = curY + SC(28);
            int rowH = SC(19);
            int innerW = cardW - SC(24);
            int innerX = pad + SC(12);

            WCHAR modeDisplay[64];
            wcsncpy(modeDisplay, st ? st->modeName : L"Disconnected", 63);
            modeDisplay[63] = L'\0';

            DrawCardRow(memDC, innerX, innerY + (rowH * 0), innerW, rowH, L"Connection Mode", modeDisplay, colTextSec, colTextPri, s_hFontRow);
            DrawCardRow(memDC, innerX, innerY + (rowH * 1), innerW, rowH, L"Estimated Remaining", estBuf, colTextSec, colTextPri, s_hFontRow);
            DrawCardRow(memDC, innerX, innerY + (rowH * 2), innerW, rowH, L"Full Endurance", enduranceStr, colTextSec, colTextPri, s_hFontRow);

            if (s_detailsTargetModel == MODEL_COREM1) {
                WCHAR telemRow4[64] = {0};
                if (st && st->pollingRateHz > 0) {
                    _snwprintf(telemRow4, 64, L"%d Hz", st->pollingRateHz);
                } else {
                    wcscpy(telemRow4, (st && st->mode != MODE_DISCONNECTED) ? L"Measuring..." : L"Offline");
                }
                DrawCardRow(memDC, innerX, innerY + (rowH * 3), innerW, rowH, L"Polling Rate", telemRow4, colTextSec, colTextPri, s_hFontRow);
            }

            curY += card1H + SC(10);

            // Card 2: CONSUMPTION RATES
            int card2H = SC(92);
            DrawCardPanel(memDC, pad, curY, cardW, card2H, colCardBg, colBorder, SC(8));
            SelectObject(memDC, s_hFontSecHdr);
            SetTextColor(memDC, colHeader);
            TextOutW(memDC, pad + SC(12), curY + SC(8), L"CONSUMPTION RATES", 17);

            innerY = curY + SC(28);
            DrawCardRow(memDC, innerX, innerY + (rowH * 0), innerW, rowH, L"Discharge Rate", dRateStr, colTextSec, colTextPri, s_hFontRow);
            DrawCardRow(memDC, innerX, innerY + (rowH * 1), innerW, rowH, L"Charge Rate", cRateStr, colTextSec, colTextPri, s_hFontRow);

            BOOL isCalibrated = stats ? BatteryStats_IsCalibrated(stats) : FALSE;
            const WCHAR *telemStatus = isFiltering ? L"Stabilizing (ADC filter)"
                                                   : (isCalibrated ? L"Calibrated (Profiling)" : L"Calibrating (Learning)");
            COLORREF colTelem = isFiltering ? (isDark ? RGB(251, 191, 36) : RGB(217, 119, 6))
                                            : (isCalibrated ? colAccent : colMuted);

            DrawCardRow(memDC, innerX, innerY + (rowH * 2), innerW, rowH, L"Telemetry Status",
                        telemStatus, colTextSec, colTelem, s_hFontRow);

            curY += card2H + SC(10);

            // Card 3: ALERT CONFIGURATIONS
            int card3H = SC(112);
            DrawCardPanel(memDC, pad, curY, cardW, card3H, colCardBg, colBorder, SC(8));
            SelectObject(memDC, s_hFontSecHdr);
            SetTextColor(memDC, colHeader);
            TextOutW(memDC, pad + SC(12), curY + SC(8), L"ALERT CONFIGURATIONS", 20);

            innerY = curY + SC(28);
            WCHAR alertLabel[64];
            int lowThresh = ds ? ds->lowAlertThreshold : 20;
            _snwprintf(alertLabel, 64, L"Low Battery Alert (<=%d%%)", lowThresh);
            BOOL lowEn = ds ? ds->lowBatteryAlert : FALSE;
            DrawCardRow(memDC, innerX, innerY + (rowH * 0), innerW, rowH, alertLabel,
                        lowEn ? L"Enabled" : L"Disabled", colTextSec, lowEn ? colAccent : colMuted, s_hFontRow);

            int longThresh = ds ? ds->longevityAlertThreshold : 85;
            _snwprintf(alertLabel, 64, L"Longevity Alert (%d%%)", longThresh);
            BOOL longEn = ds ? ds->longevityAlert : FALSE;
            DrawCardRow(memDC, innerX, innerY + (rowH * 1), innerW, rowH, alertLabel,
                        longEn ? L"Enabled" : L"Disabled", colTextSec, longEn ? colAccent : colMuted, s_hFontRow);

            int highThresh = ds ? ds->highAlertThreshold : 100;
            _snwprintf(alertLabel, 64, L"Full Battery Alert (%d%%)", highThresh);
            BOOL highEn = ds ? ds->highBatteryAlert : FALSE;
            DrawCardRow(memDC, innerX, innerY + (rowH * 2), innerW, rowH, alertLabel,
                        highEn ? L"Enabled" : L"Disabled", colTextSec, highEn ? colAccent : colMuted, s_hFontRow);

            DrawCardRow(memDC, innerX, innerY + (rowH * 3), innerW, rowH, L"Start with Windows",
                        g_Settings.startWithWindows ? L"Enabled" : L"Disabled",
                        colTextSec, g_Settings.startWithWindows ? colAccent : colMuted, s_hFontRow);

            curY += card3H + SC(12);

            // Close Button
            int btnW = SC(90);
            int btnH = SC(28);
            int btnX = width - pad - btnW;
            int btnY = curY;

            s_closeButtonRect.left = btnX;
            s_closeButtonRect.top = btnY;
            s_closeButtonRect.right = btnX + btnW;
            s_closeButtonRect.bottom = btnY + btnH;

            DrawCardPanel(memDC, btnX, btnY, btnW, btnH, colBtnBg, colBtnBdr, SC(6));
            SelectObject(memDC, s_hFontBtn);
            SetTextColor(memDC, colBtnText);
            RECT textRect = s_closeButtonRect;
            OffsetRect(&textRect, 0, -SC(1));
            DrawTextW(memDC, L"Close", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            #undef SC

            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);
            POINT pt = { mx, my };
            BOOL hover = PtInRect(&s_closeButtonRect, pt);
            if (hover != s_closeHover) {
                s_closeHover = hover;
                InvalidateRect(hWnd, &s_closeButtonRect, FALSE);
                if (hover) {
                    TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
                    TrackMouseEvent(&tme);
                }
            }
            break;
        }

        case WM_MOUSELEAVE: {
            if (s_closeHover) {
                s_closeHover = FALSE;
                InvalidateRect(hWnd, &s_closeButtonRect, FALSE);
            }
            break;
        }

        case WM_LBUTTONUP: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);
            POINT pt = { mx, my };
            if (PtInRect(&s_closeButtonRect, pt)) {
                DestroyWindow(hWnd);
            }
            break;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY: {
            s_hDetailsWnd = NULL;
            s_closeHover = FALSE;
            CleanupDetailsFonts();
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void ShowDetailsDialog(HWND hWnd, DeviceModel model) {
    s_detailsTargetModel = model;
    const WCHAR *dlgTitle = (model == MODEL_COREM1) ? L"Core Monitor - Core M1 Mouse Telemetry" : L"Core Monitor - Core75 Keyboard Telemetry";

    HDC hdc = GetDC(NULL);
    int dpi = 96;
    if (s_pfnGetDpiForWindow && hWnd) dpi = s_pfnGetDpiForWindow(hWnd);
    if (dpi <= 0) dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    if (dpi <= 0) dpi = 96;

    int clientW = MulDiv(400, dpi, 96);
    int clientH = MulDiv((model == MODEL_COREM1) ? 451 : 432, dpi, 96);

    RECT rc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, 0);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
        SetWindowTextW(s_hDetailsWnd, dlgTitle);
        SetWindowPos(s_hDetailsWnd, HWND_TOP, 0, 0, winW, winH, SWP_NOMOVE | SWP_SHOWWINDOW);
        ShowWindow(s_hDetailsWnd, SW_RESTORE);
        SetForegroundWindow(s_hDetailsWnd);
        BringWindowToTop(s_hDetailsWnd);
        InvalidateRect(s_hDetailsWnd, NULL, TRUE);
        return;
    }

    HINSTANCE hInstance = GetModuleHandleW(NULL);

    if (!s_detailsClassRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = DetailsWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = DETAILS_CLASS_NAME;
        RegisterClassExW(&wc);
        s_detailsClassRegistered = TRUE;
    }

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int posX = workArea.left + (workArea.right - workArea.left - winW) / 2;
    int posY = workArea.top + (workArea.bottom - workArea.top - winH) / 2;

    s_hDetailsWnd = CreateWindowExW(
        0, DETAILS_CLASS_NAME, dlgTitle,
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        posX, posY, winW, winH,
        hWnd, NULL, hInstance, NULL
    );

    if (s_hDetailsWnd) {
        BOOL isDark = IsAppInDarkMode();
        ApplyWindowTheme(s_hDetailsWnd, isDark);
        ShowWindow(s_hDetailsWnd, SW_SHOW);
        UpdateWindow(s_hDetailsWnd);
        SetForegroundWindow(s_hDetailsWnd);
    }
}

// -----------------------------------------------------------------------------
// Polling & State Management
// -----------------------------------------------------------------------------
static void PollAndUpdate(DeviceModel model) {
    DWORD now = GetTickCount();
    DWORD sleepMs = 0;
    DWORD *pLastInputTick = NULL;
    DWORD *pLastRadioTick = NULL;
    DeviceStatus *st = NULL;
    BatteryStatsContext *stats = NULL;
    DeviceSettings *ds = NULL;
    NOTIFYICONDATAW *nid = NULL;
    BOOL (*pfnPoll)(DeviceStatus*, BOOL) = NULL;

#if defined(BUILD_TARGET_CORE75)
    if (model == MODEL_CORE75) {
        sleepMs = (DWORD)CORE75_DEFAULT_SLEEP_MINUTES * 60 * 1000;
        pLastInputTick = &s_lastCore75InputTick;
        pLastRadioTick = &s_lastCore75RadioQueryTick;
        st = &s_statusCore75;
        stats = &s_statsCore75;
        ds = &g_Settings.core75;
        nid = &s_nidCore75;
        pfnPoll = DeviceCore75_Poll;
    }
#endif
#if defined(BUILD_TARGET_COREM1)
    if (model == MODEL_COREM1) {
        sleepMs = (DWORD)COREM1_DEFAULT_SLEEP_MINUTES * 60 * 1000;
        pLastInputTick = &s_lastCoreM1InputTick;
        pLastRadioTick = &s_lastCoreM1RadioQueryTick;
        st = &s_statusCoreM1;
        stats = &s_statsCoreM1;
        ds = &g_Settings.corem1;
        nid = &s_nidCoreM1;
        pfnPoll = DeviceCoreM1_Poll;
    }
#endif

    if (!pLastInputTick || !pLastRadioTick || !st || !stats || !ds || !nid || !pfnPoll) return;

    DWORD idleMs = (*pLastInputTick != 0) ? (DWORD)(now - *pLastInputTick) : (sleepMs + 1);
    BOOL isSleeping = FALSE;
    BOOL allowWirelessQuery = FALSE;
    BOOL wasWakeOrRefresh = (*pLastRadioTick == 0);

    if (wasWakeOrRefresh) {
        allowWirelessQuery = TRUE;
    } else if (st->mode == MODE_2P4G) {
        if (idleMs >= sleepMs) {
            isSleeping = TRUE;
            allowWirelessQuery = FALSE;
        } else {
            if ((DWORD)(now - *pLastInputTick) < 60000 && (DWORD)(now - *pLastRadioTick) >= 300000) {
                allowWirelessQuery = TRUE;
            }
        }
    } else if (st->mode == MODE_WIRED) {
        allowWirelessQuery = TRUE;
    }

    BOOL liveSuccess = pfnPoll(st, allowWirelessQuery);

    if (allowWirelessQuery && st->mode == MODE_2P4G) {
        *pLastRadioTick = now;
        if (liveSuccess && *pLastInputTick == 0) *pLastInputTick = now;
    } else if (st->mode == MODE_WIRED) {
        *pLastRadioTick = now;
        *pLastInputTick = now;
    }

    if (st->mode == MODE_2P4G) {
        if (liveSuccess) {
            isSleeping = (model == MODEL_COREM1) ? st->isSleeping : FALSE;
        } else if (wasWakeOrRefresh || idleMs >= sleepMs || *pLastInputTick == 0) {
            isSleeping = TRUE;
            *pLastInputTick = 0;
        } else {
            isSleeping = FALSE;
        }
        st->isSleeping = isSleeping;
        int eff = (st->batteryPercent >= 0) ? st->batteryPercent : st->lastKnownPercent;
        st->batteryPercent = eff;
        st->lastKnownPercent = eff;
    }

    BOOL isDisc = (st->mode == MODE_DISCONNECTED);
    int effPercent = (st->batteryPercent >= 0) ? st->batteryPercent : st->lastKnownPercent;
    BatteryStats_Record(stats, effPercent, st->isCharging, isDisc);

    CheckDeviceAlerts(model, ds, nid, st->batteryPercent, st->isCharging, isDisc);

    UpdateTrayIcon(model);
    SyncAnimationTimer();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

static void ResetDevicePowerState(HWND hWnd, DeviceModel model) {
#if defined(BUILD_TARGET_CORE75)
    if (model == MODEL_CORE75) {
        KillTimer(hWnd, IDT_POLL_TIMER_CORE75);
        s_lastCore75InputTick = 0;
        s_lastCore75RadioQueryTick = 0;
        s_statusCore75.isSleeping = TRUE;
        BatteryStats_ResetSessionBaseline(&s_statsCore75);
        DeviceCore75_ResetBaseline();
        DeviceCore75_InvalidateCache();
    }
#endif
#if defined(BUILD_TARGET_COREM1)
    if (model == MODEL_COREM1) {
        KillTimer(hWnd, IDT_POLL_TIMER_COREM1);
        s_lastCoreM1InputTick = 0;
        s_lastCoreM1RadioQueryTick = 0;
        s_statusCoreM1.isSleeping = TRUE;
        BatteryStats_ResetSessionBaseline(&s_statsCoreM1);
        DeviceCoreM1_ResetBaseline();
        DeviceCoreM1_InvalidateCache();
    }
#endif
}

static void ResetAllDevicesPowerState(HWND hWnd) {
#if defined(BUILD_TARGET_CORE75)
    ResetDevicePowerState(hWnd, MODEL_CORE75);
#endif
#if defined(BUILD_TARGET_COREM1)
    ResetDevicePowerState(hWnd, MODEL_COREM1);
#endif
}

static void RestartAllDeviceTimers(HWND hWnd, UINT elapseMs) {
#if defined(BUILD_TARGET_CORE75)
    SetTimer(hWnd, IDT_POLL_TIMER_CORE75, elapseMs, NULL);
#endif
#if defined(BUILD_TARGET_COREM1)
    SetTimer(hWnd, IDT_POLL_TIMER_COREM1, elapseMs, NULL);
#endif
}

// -----------------------------------------------------------------------------
// Context Menus
// -----------------------------------------------------------------------------
static void ShowContextMenu(HWND hWnd, DeviceModel model) {
    if (s_pfnSetPreferredAppMode) s_pfnSetPreferredAppMode(APPMODE_ALLOWDARK);
    if (s_pfnAllowDarkModeForWindow) s_pfnAllowDarkModeForWindow(hWnd, TRUE);
    if (s_pfnFlushMenuThemes) s_pfnFlushMenuThemes();

    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    const WCHAR *modelHeader = (model == MODEL_COREM1) ? L"Core Monitor - Core M1" : L"Core Monitor - Core75";
    const DeviceStatus *st = NULL;
    const BatteryStatsContext *stats = NULL;
    DeviceSettings *ds = NULL;

#if defined(BUILD_TARGET_CORE75)
    if (model == MODEL_CORE75) {
        st = &s_statusCore75;
        stats = &s_statsCore75;
        ds = &g_Settings.core75;
    }
#endif
#if defined(BUILD_TARGET_COREM1)
    if (model == MODEL_COREM1) {
        st = &s_statusCoreM1;
        stats = &s_statsCoreM1;
        ds = &g_Settings.corem1;
    }
#endif

    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_TITLE, modelHeader);

    // Status line
    WCHAR statusLine[128];
    int effPercent = (st && st->batteryPercent >= 0) ? st->batteryPercent : (st ? st->lastKnownPercent : -1);
    const WCHAR *modeName = st ? st->modeName : L"Disconnected";

    if (!st || st->mode == MODE_DISCONNECTED) {
        _snwprintf(statusLine, 128, L"Status: Disconnected");
    } else if (st->isSleeping) {
        if (effPercent < 0) {
            _snwprintf(statusLine, 128, L"Status: Asleep (Wake device) - [2.4GHz]");
        } else {
            _snwprintf(statusLine, 128, L"Status: %d%% - [Asleep - 2.4GHz]", effPercent);
        }
    } else if (effPercent < 0) {
        _snwprintf(statusLine, 128, L"Status: Connecting... - [%s]", modeName);
    } else if (st->isCharging) {
        _snwprintf(statusLine, 128, L"Status: %d%% - [%s - Charging]", effPercent, modeName);
    } else {
        _snwprintf(statusLine, 128, L"Status: %d%% - [%s]", effPercent, modeName);
    }
    statusLine[127] = L'\0';
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_INFO, statusLine);

    // Estimate line
    WCHAR estLine[128], estBuf[64];
    if (stats && st) {
        BatteryStats_FormatRemainingText(stats, effPercent, st->isCharging, st->isSleeping, (st->mode == MODE_DISCONNECTED), estBuf, 64);
    } else {
        wcscpy(estBuf, L"Offline");
    }
    _snwprintf(estLine, 128, L"Estimate: %s", estBuf);
    estLine[127] = L'\0';
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_EST, estLine);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh Now");
    AppendMenuW(hMenu, MF_STRING, IDM_DETAILS, L"Battery Details && Statistics...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // 1. Configure Icon expandable submenu
    HMENU hIconMenu = CreatePopupMenu();
    CenterIconStyle curStyle = ds ? ds->centerStyle : ICON_CENTER_PERCENT;
    AppendMenuW(hIconMenu, (curStyle == ICON_CENTER_RING_ONLY ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_STYLE_RING_ONLY, L"Show Ring-Only");
    AppendMenuW(hIconMenu, (curStyle == ICON_CENTER_PERCENT ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_STYLE_PERCENT, L"Show Battery Percentage");
    const WCHAR *glyphLabel = (model == MODEL_COREM1) ? L"Show Mouse Icon" : L"Show Keyboard Icon";
    AppendMenuW(hIconMenu, (curStyle == ICON_CENTER_GLYPH ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_STYLE_GLYPH, glyphLabel);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hIconMenu, L"Configure Icon");

    // 2. Low Battery Alert submenu
    HMENU hLowMenu = CreatePopupMenu();
    BOOL lowEn = ds ? ds->lowBatteryAlert : FALSE;
    int lowThresh = ds ? ds->lowAlertThreshold : 20;
    AppendMenuW(hLowMenu, (lowEn ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LOW_ENABLE, L"Enabled");
    AppendMenuW(hLowMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hLowMenu, (lowThresh == 10 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LOW_10, L"10%");
    AppendMenuW(hLowMenu, (lowThresh == 15 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LOW_15, L"15%");
    AppendMenuW(hLowMenu, (lowThresh == 20 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LOW_20, L"20%");
    WCHAR subMenuLabel[64];
    _snwprintf(subMenuLabel, 64, L"Low Battery Alert (<=%d%%)", lowThresh);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hLowMenu, subMenuLabel);

    // 3. Battery Longevity Alert submenu
    HMENU hLongMenu = CreatePopupMenu();
    BOOL longEn = ds ? ds->longevityAlert : FALSE;
    int longThresh = ds ? ds->longevityAlertThreshold : 85;
    AppendMenuW(hLongMenu, (longEn ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LONG_ENABLE, L"Enabled");
    AppendMenuW(hLongMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hLongMenu, (longThresh == 80 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LONG_80, L"80%");
    AppendMenuW(hLongMenu, (longThresh == 85 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_LONG_85, L"85%");
    _snwprintf(subMenuLabel, 64, L"Battery Longevity Alert (%d%%)", longThresh);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hLongMenu, subMenuLabel);

    // 4. Full Battery Alert submenu
    HMENU hFullMenu = CreatePopupMenu();
    BOOL highEn = ds ? ds->highBatteryAlert : FALSE;
    int highThresh = ds ? ds->highAlertThreshold : 100;
    AppendMenuW(hFullMenu, (highEn ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_FULL_ENABLE, L"Enabled");
    AppendMenuW(hFullMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFullMenu, (highThresh == 95 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_FULL_95, L"95%");
    AppendMenuW(hFullMenu, (highThresh == 100 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, IDM_FULL_100, L"100%");
    _snwprintf(subMenuLabel, 64, L"Full Battery Alert (%d%%)", highThresh);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFullMenu, subMenuLabel);

#if defined(BUILD_TARGET_CORE75)
    if (model == MODEL_CORE75) {
        UINT capsFlags = MF_STRING | (g_Settings.core75.capsLockIndicator ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hMenu, capsFlags, IDM_CAPSLOCK_INDICATOR, L"Caps Lock Indicator");
    }
#endif

    // Run on startup
    UINT autoFlags = MF_STRING | (g_Settings.startWithWindows ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autoFlags, IDM_AUTOSTART, L"Start with Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    int cmd = TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, hWnd, NULL);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    switch (cmd) {
        case IDM_REFRESH:
#if defined(BUILD_TARGET_CORE75)
            if (model == MODEL_CORE75) {
                s_lastCore75RadioQueryTick = 0;
                DeviceCore75_InvalidateCache();
                DeviceCore75_ForceDirectNextPoll();
                PollAndUpdate(MODEL_CORE75);
            }
#endif
#if defined(BUILD_TARGET_COREM1)
            if (model == MODEL_COREM1) {
                s_lastCoreM1RadioQueryTick = 0;
                DeviceCoreM1_InvalidateCache();
                DeviceCoreM1_ForceDirectNextPoll();
                PollAndUpdate(MODEL_COREM1);
            }
#endif
            break;

        case IDM_DETAILS:
#if defined(BUILD_TARGET_CORE75)
            if (model == MODEL_CORE75) ClearDeviceToast(&s_nidCore75);
#endif
#if defined(BUILD_TARGET_COREM1)
            if (model == MODEL_COREM1) ClearDeviceToast(&s_nidCoreM1);
#endif
            ShowDetailsDialog(hWnd, model);
            break;

        case IDM_STYLE_RING_ONLY:
            if (ds) ds->centerStyle = ICON_CENTER_RING_ONLY;
            Settings_Save();
            UpdateAllTrayIcons();
            break;
        case IDM_STYLE_PERCENT:
            if (ds) ds->centerStyle = ICON_CENTER_PERCENT;
            Settings_Save();
            UpdateAllTrayIcons();
            break;
        case IDM_STYLE_GLYPH:
            if (ds) ds->centerStyle = ICON_CENTER_GLYPH;
            Settings_Save();
            UpdateAllTrayIcons();
            break;

        case IDM_LOW_ENABLE:
            if (ds) ds->lowBatteryAlert = !ds->lowBatteryAlert;
            goto save_and_redraw;
        case IDM_LOW_10:
            if (ds) ds->lowAlertThreshold = 10;
            goto save_and_redraw;
        case IDM_LOW_15:
            if (ds) ds->lowAlertThreshold = 15;
            goto save_and_redraw;
        case IDM_LOW_20:
            if (ds) ds->lowAlertThreshold = 20;
            goto save_and_redraw;

        case IDM_LONG_ENABLE:
            if (ds) ds->longevityAlert = !ds->longevityAlert;
            goto save_and_redraw;
        case IDM_LONG_80:
            if (ds) ds->longevityAlertThreshold = 80;
            goto save_and_redraw;
        case IDM_LONG_85:
            if (ds) ds->longevityAlertThreshold = 85;
            goto save_and_redraw;

        case IDM_FULL_ENABLE:
            if (ds) ds->highBatteryAlert = !ds->highBatteryAlert;
            goto save_and_redraw;
        case IDM_FULL_95:
            if (ds) ds->highAlertThreshold = 95;
            goto save_and_redraw;
        case IDM_FULL_100:
            if (ds) ds->highAlertThreshold = 100;
            goto save_and_redraw;

        case IDM_CAPSLOCK_INDICATOR:
#if defined(BUILD_TARGET_CORE75)
            if (model == MODEL_CORE75) {
                g_Settings.core75.capsLockIndicator = !g_Settings.core75.capsLockIndicator;
                UpdateTrayIcon(MODEL_CORE75);
            }
#endif
            goto save_and_redraw;

        case IDM_AUTOSTART:
            Settings_SetAutostart(!g_Settings.startWithWindows);
            goto save_and_redraw;

        save_and_redraw:
            Settings_Save();
            if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) InvalidateRect(s_hDetailsWnd, NULL, TRUE);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
    }
}

// -----------------------------------------------------------------------------
// Window Procedure
// -----------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == s_uTaskbarRestartMsg && s_uTaskbarRestartMsg != 0) {
#if defined(BUILD_TARGET_CORE75)
        s_nidCore75.hIcon = s_hCurrentIconCore75;
        Shell_NotifyIconW(NIM_ADD, &s_nidCore75);
        UpdateTrayIcon(MODEL_CORE75);
#endif
#if defined(BUILD_TARGET_COREM1)
        s_nidCoreM1.hIcon = s_hCurrentIconCoreM1;
        Shell_NotifyIconW(NIM_ADD, &s_nidCoreM1);
        UpdateTrayIcon(MODEL_COREM1);
#endif
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            // Register Raw Input sinks
            RAWINPUTDEVICE rid[2] = {0};
            UINT ridCount = 0;

#if defined(BUILD_TARGET_CORE75)
            rid[ridCount].usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
            rid[ridCount].usUsage     = 0x06; // KEYBOARD
            rid[ridCount].dwFlags     = RIDEV_INPUTSINK;
            rid[ridCount].hwndTarget  = hWnd;
            ridCount++;
            s_lastCore75InputTick = 0;
            s_lastCore75RadioQueryTick = 0;
#endif

#if defined(BUILD_TARGET_COREM1)
            rid[ridCount].usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
            rid[ridCount].usUsage     = 0x02; // MOUSE
            rid[ridCount].dwFlags     = RIDEV_INPUTSINK;
            rid[ridCount].hwndTarget  = hWnd;
            ridCount++;
            s_lastCoreM1InputTick = 0;
            s_lastCoreM1RadioQueryTick = 0;
#endif

            if (ridCount > 0) {
                RegisterRawInputDevices(rid, ridCount, sizeof(RAWINPUTDEVICE));
            }

            DEV_BROADCAST_DEVICEINTERFACE_W filter = {0};
            filter.dbcc_size = sizeof(filter);
            filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            s_hDevNotify = RegisterDeviceNotificationW(hWnd, &filter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

            if (s_pfnWTSRegisterSessionNotification) {
                s_isWtsRegistered = s_pfnWTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);
            }

            if (s_pfnRegisterPowerSettingNotification) {
                s_hPowerNotify = s_pfnRegisterPowerSettingNotification(hWnd, &GUID_CONSOLE_DISPLAY_STATE_M, DEVICE_NOTIFY_WINDOW_HANDLE);
            }

#if defined(BUILD_TARGET_CORE75)
            SetTimer(hWnd, IDT_POLL_TIMER_CORE75, 60000, NULL);
#endif
#if defined(BUILD_TARGET_COREM1)
            SetTimer(hWnd, IDT_POLL_TIMER_COREM1, 60000, NULL);
#endif
            break;
        }

        case WM_INPUT: {
            HRAWINPUT hRaw = (HRAWINPUT)lParam;
#if defined(BUILD_TARGET_CORE75)
            BOOL kbWake = FALSE;
            BOOL capsChanged = FALSE;
            if (DeviceCore75_HandleRawInput(hRaw, &kbWake, &capsChanged)) {
                s_lastCore75InputTick = GetTickCount();
                if (capsChanged) {
                    s_statusCore75.isCapsLock = DeviceCore75_IsCapsLock();
                }
                if ((s_statusCore75.isSleeping || s_lastCore75RadioQueryTick == 0) && s_statusCore75.mode == MODE_2P4G) {
                    s_statusCore75.isSleeping = FALSE;
                    UpdateTrayIcon(MODEL_CORE75);
                    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd) && s_detailsTargetModel == MODEL_CORE75) {
                        InvalidateRect(s_hDetailsWnd, NULL, TRUE);
                    }
                    s_lastCore75RadioQueryTick = 0;
                    SetTimer(hWnd, IDT_POLL_TIMER_CORE75, 150, NULL);
                } else if (capsChanged && g_Settings.core75.capsLockIndicator) {
                    UpdateTrayIcon(MODEL_CORE75);
                }
            }
#endif
#if defined(BUILD_TARGET_COREM1)
            BOOL mouseWake = FALSE;
            if (DeviceCoreM1_HandleRawInput(hRaw, s_statusCoreM1.isSleeping, &mouseWake)) {
                s_lastCoreM1InputTick = GetTickCount();
                if ((s_statusCoreM1.isSleeping || mouseWake || s_lastCoreM1RadioQueryTick == 0) && s_statusCoreM1.mode == MODE_2P4G) {
                    s_statusCoreM1.isSleeping = FALSE;
                    UpdateTrayIcon(MODEL_COREM1);
                    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd) && s_detailsTargetModel == MODEL_COREM1) {
                        InvalidateRect(s_hDetailsWnd, NULL, TRUE);
                    }
                    s_lastCoreM1RadioQueryTick = 0;
                    SetTimer(hWnd, IDT_POLL_TIMER_COREM1, 150, NULL);
                }
            }
#endif
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        case WM_TIMER: {
            if (wParam == IDT_POLL_TIMER_CORE75 || wParam == IDT_POLL_TIMER_COREM1) {
                DeviceModel model = (wParam == IDT_POLL_TIMER_COREM1) ? MODEL_COREM1 : MODEL_CORE75;
                SetTimer(hWnd, (UINT_PTR)wParam, 60000, NULL);
                s_isResumingFromSuspend = FALSE;
                PollAndUpdate(model);
                if (s_hDetailsWnd && IsWindow(s_hDetailsWnd) && s_detailsTargetModel == model) {
                    InvalidateRect(s_hDetailsWnd, NULL, TRUE);
                }
            } else if (wParam == IDT_CLICK_TIMER_CORE75 || wParam == IDT_CLICK_TIMER_COREM1) {
                KillTimer(hWnd, (UINT_PTR)wParam);
                DeviceModel model = (wParam == IDT_CLICK_TIMER_COREM1) ? MODEL_COREM1 : MODEL_CORE75;
#if defined(BUILD_TARGET_CORE75)
                if (model == MODEL_CORE75) DeviceCore75_ForceDirectNextPoll();
#endif
#if defined(BUILD_TARGET_COREM1)
                if (model == MODEL_COREM1) DeviceCoreM1_ForceDirectNextPoll();
#endif
                PollAndUpdate(model);
                DWORD now = GetTickCount();
                DWORD *pLastToast = NULL;
                NOTIFYICONDATAW *pToastNid = NULL;
                DeviceStatus *pSt = NULL;
                BatteryStatsContext *pStats = NULL;
#if defined(BUILD_TARGET_CORE75)
                if (model == MODEL_CORE75) {
                    pLastToast = &s_lastCore75ManualToastTick;
                    pToastNid = &s_nidCore75;
                    pSt = &s_statusCore75;
                    pStats = &s_statsCore75;
                }
#endif
#if defined(BUILD_TARGET_COREM1)
                if (model == MODEL_COREM1) {
                    pLastToast = &s_lastCoreM1ManualToastTick;
                    pToastNid = &s_nidCoreM1;
                    pSt = &s_statusCoreM1;
                    pStats = &s_statsCoreM1;
                }
#endif
                if (pLastToast && (now - *pLastToast >= TOAST_MANUAL_COOLDOWN_MS)) {
                    *pLastToast = now;
                    if (pToastNid && pSt && pStats) {
                        FormatDeviceTooltip(model, pSt, pStats, pToastNid->szInfo, 255);
                        const WCHAR *toastTitle = (model == MODEL_COREM1) ? L"URX Core M1 Status" : L"URX Core75 Status";
                        ShowDeviceToast(pToastNid, toastTitle, pToastNid->szInfo, NIIF_INFO);
                    }
                }
            } else if (wParam == IDT_ANIM_TIMER) {
                s_breathPhase += 0.13f;
                if (s_breathPhase > 2.0f * PI) s_breathPhase -= 2.0f * PI;
                UpdateAllTrayIcons();
            }
            break;
        }

        case WM_DEVICECHANGE: {
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
                ResetAllDevicesPowerState(hWnd);
                RestartAllDeviceTimers(hWnd, 150);
            }
            break;
        }

        case WM_SETTINGCHANGE: {
            if (s_pfnSetPreferredAppMode) s_pfnSetPreferredAppMode(APPMODE_ALLOWDARK);
            if (s_pfnFlushMenuThemes) s_pfnFlushMenuThemes();
            UpdateAllTrayIcons();
            if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
                BOOL isDark = IsAppInDarkMode();
                ApplyWindowTheme(s_hDetailsWnd, isDark);
                InvalidateRect(s_hDetailsWnd, NULL, TRUE);
            }
            break;
        }

        case WM_POWERBROADCAST: {
            if (wParam == PBT_APMSUSPEND) {
                s_isResumingFromSuspend = TRUE;
                ResetAllDevicesPowerState(hWnd);
            } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                s_isResumingFromSuspend = TRUE;
                ResetAllDevicesPowerState(hWnd);
                if (!s_isSessionLocked && !s_isDisplayOff) RestartAllDeviceTimers(hWnd, 1500);
            } else if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
                POWERBROADCAST_SETTING_M *pSetting = (POWERBROADCAST_SETTING_M*)lParam;
                if (IsEqualGUID(&pSetting->PowerSetting, &GUID_CONSOLE_DISPLAY_STATE_M)) {
                    DWORD displayState = 0;
                    memcpy(&displayState, pSetting->Data, (pSetting->DataLength < sizeof(DWORD)) ? pSetting->DataLength : sizeof(DWORD));
                    if (displayState == 0) {
                        s_isDisplayOff = TRUE;
                        ResetAllDevicesPowerState(hWnd);
                    } else if (displayState == 1 || displayState == 2) {
                        s_isDisplayOff = FALSE;
                        if (!s_isSessionLocked && !s_isResumingFromSuspend) {
                            ResetAllDevicesPowerState(hWnd);
                            RestartAllDeviceTimers(hWnd, 500);
                        }
                    }
                }
            }
            break;
        }

        case WM_WTSSESSION_CHANGE: {
            if (wParam == WTS_SESSION_LOCK) {
                s_isSessionLocked = TRUE;
                ResetAllDevicesPowerState(hWnd);
            } else if (wParam == WTS_SESSION_UNLOCK) {
                s_isSessionLocked = FALSE;
                if (!s_isDisplayOff) {
                    ResetAllDevicesPowerState(hWnd);
                    RestartAllDeviceTimers(hWnd, 500);
                }
            }
            break;
        }

        case WM_TRAY_CALLBACK: {
            UINT uID = (UINT)wParam;
            DeviceModel targetModel = (uID == 1002) ? MODEL_COREM1 : MODEL_CORE75;

            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
#if defined(BUILD_TARGET_CORE75)
                if (targetModel == MODEL_CORE75) KillTimer(hWnd, IDT_CLICK_TIMER_CORE75);
#endif
#if defined(BUILD_TARGET_COREM1)
                if (targetModel == MODEL_COREM1) KillTimer(hWnd, IDT_CLICK_TIMER_COREM1);
#endif
                ShowContextMenu(hWnd, targetModel);
            } else if (lParam == WM_LBUTTONDBLCLK) {
#if defined(BUILD_TARGET_CORE75)
                if (targetModel == MODEL_CORE75) {
                    KillTimer(hWnd, IDT_CLICK_TIMER_CORE75);
                    ClearDeviceToast(&s_nidCore75);
                }
#endif
#if defined(BUILD_TARGET_COREM1)
                if (targetModel == MODEL_COREM1) {
                    KillTimer(hWnd, IDT_CLICK_TIMER_COREM1);
                    ClearDeviceToast(&s_nidCoreM1);
                }
#endif
                ShowDetailsDialog(hWnd, targetModel);
            } else if (lParam == WM_LBUTTONUP) {
                UINT dblClickTime = GetDoubleClickTime();
                if (dblClickTime < 180) dblClickTime = 180;
                if (dblClickTime > 400) dblClickTime = 400;

#if defined(BUILD_TARGET_CORE75)
                if (targetModel == MODEL_CORE75) SetTimer(hWnd, IDT_CLICK_TIMER_CORE75, dblClickTime, NULL);
#endif
#if defined(BUILD_TARGET_COREM1)
                if (targetModel == MODEL_COREM1) SetTimer(hWnd, IDT_CLICK_TIMER_COREM1, dblClickTime, NULL);
#endif
            }
            break;
        }

        case WM_DESTROY: {
            if (s_hDevNotify) {
                UnregisterDeviceNotification(s_hDevNotify);
                s_hDevNotify = NULL;
            }
            if (s_hPowerNotify && s_pfnUnregisterPowerSettingNotification) {
                s_pfnUnregisterPowerSettingNotification(s_hPowerNotify);
                s_hPowerNotify = NULL;
            }
            if (s_isWtsRegistered && s_pfnWTSUnRegisterSessionNotification) {
                s_pfnWTSUnRegisterSessionNotification(hWnd);
                s_isWtsRegistered = FALSE;
            }
#if defined(BUILD_TARGET_CORE75)
            KillTimer(hWnd, IDT_POLL_TIMER_CORE75);
            KillTimer(hWnd, IDT_CLICK_TIMER_CORE75);
#endif
#if defined(BUILD_TARGET_COREM1)
            KillTimer(hWnd, IDT_POLL_TIMER_COREM1);
            KillTimer(hWnd, IDT_CLICK_TIMER_COREM1);
#endif
            KillTimer(hWnd, IDT_ANIM_TIMER);

            if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
                DestroyWindow(s_hDetailsWnd);
                s_hDetailsWnd = NULL;
            }

#if defined(BUILD_TARGET_CORE75)
            ClearDeviceToast(&s_nidCore75);
            Shell_NotifyIconW(NIM_DELETE, &s_nidCore75);
            if (s_hCurrentIconCore75) DestroyIcon(s_hCurrentIconCore75);
            BatteryStats_Save(&s_statsCore75);
            DeviceCore75_Cleanup();
#endif
#if defined(BUILD_TARGET_COREM1)
            ClearDeviceToast(&s_nidCoreM1);
            Shell_NotifyIconW(NIM_DELETE, &s_nidCoreM1);
            if (s_hCurrentIconCoreM1) DestroyIcon(s_hCurrentIconCoreM1);
            BatteryStats_Save(&s_statsCoreM1);
            DeviceCoreM1_Cleanup();
#endif

            HidCommon_Cleanup();
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    InitDarkMode();
    InitPowerAndSessionHooks();

    if (s_pfnSetCurrentProcessExplicitAppUserModelID) {
        s_pfnSetCurrentProcessExplicitAppUserModelID(L"CoreMonitor");
    }

    // Single Instance Guard
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindowW(CLASS_NAME, L"Core Monitor");
        if (hExistingWnd) {
            WPARAM targetId = 1001;
#if defined(BUILD_TARGET_COREM1) && !defined(BUILD_TARGET_CORE75)
            targetId = 1002;
#endif
            PostMessageW(hExistingWnd, WM_TRAY_CALLBACK, targetId, WM_LBUTTONDBLCLK);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    s_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    Settings_Load();

    if (!HidCommon_Init()) {
        MessageBoxW(NULL, L"Failed to initialize Windows HID subsystems.", L"Core Monitor Error", MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

#if defined(BUILD_TARGET_CORE75)
    DeviceCore75_Init();
    BatteryStats_Init(&s_statsCore75, L"Software\\CoreMonitor\\Stats\\Core75", L"Software\\CoreMonitor\\Stats", 0.50, 35.0);
#endif

#if defined(BUILD_TARGET_COREM1)
    DeviceCoreM1_Init();
    BatteryStats_Init(&s_statsCoreM1, L"Software\\CoreMonitor\\Stats\\CoreM1", NULL, 1.50, 50.0);
#endif

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        CloseHandle(hMutex);
        return 1;
    }

    s_hWnd = CreateWindowExW(0, CLASS_NAME, L"Core Monitor", WS_POPUP, 0, 0, 0, 0,
                             NULL, NULL, hInstance, NULL);
    if (!s_hWnd) {
        CloseHandle(hMutex);
        return 1;
    }

    if (s_pfnAllowDarkModeForWindow && s_hWnd) {
        s_pfnAllowDarkModeForWindow(s_hWnd, TRUE);
    }

#if defined(BUILD_TARGET_CORE75)
    s_nidCore75.cbSize = sizeof(NOTIFYICONDATAW);
    s_nidCore75.hWnd = s_hWnd;
    s_nidCore75.uID = 1001;
    s_nidCore75.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    s_nidCore75.uCallbackMessage = WM_TRAY_CALLBACK;
    wcsncpy(s_nidCore75.szTip, L"URX Core75", 127);

    PollAndUpdate(MODEL_CORE75);
    Shell_NotifyIconW(NIM_ADD, &s_nidCore75);
    UpdateTrayIcon(MODEL_CORE75);
#endif

#if defined(BUILD_TARGET_COREM1)
    s_nidCoreM1.cbSize = sizeof(NOTIFYICONDATAW);
    s_nidCoreM1.hWnd = s_hWnd;
    s_nidCoreM1.uID = 1002;
    s_nidCoreM1.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    s_nidCoreM1.uCallbackMessage = WM_TRAY_CALLBACK;
    wcsncpy(s_nidCoreM1.szTip, L"URX Core M1", 127);

    PollAndUpdate(MODEL_COREM1);
    Shell_NotifyIconW(NIM_ADD, &s_nidCoreM1);
    UpdateTrayIcon(MODEL_COREM1);
#endif

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(hMutex);
    return (int)msg.wParam;
}
