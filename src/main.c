#define _WIN32_WINNT 0x0601
#define _WIN32_IE    0x0600
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <dbt.h>
#include <stdio.h>
#include "battery_monitor.h"
#include "battery_stats.h"
#include "icon_generator.h"
#include "settings.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#define WM_TRAY_CALLBACK (WM_APP + 100)
#define IDT_POLL_TIMER   1001
#define IDT_ANIM_TIMER   1002
#define IDT_CLICK_TIMER  1003
#define TOAST_MANUAL_COOLDOWN_MS 4000

#define IDM_STATUS_TITLE  2001
#define IDM_STATUS_INFO   2002
#define IDM_STATUS_EST    2003
#define IDM_REFRESH       2004
#define IDM_DETAILS       2005

// Icon styles (Submenu: Configure Icon)
#define IDM_STYLE_RING_ONLY 2010
#define IDM_STYLE_PERCENT   2011
#define IDM_STYLE_KEYBOARD  2012

// Low Battery Alert Submenu
#define IDM_LOW_ENABLE      2020
#define IDM_LOW_10          2021
#define IDM_LOW_15          2022
#define IDM_LOW_20          2023

// Longevity Alert Submenu
#define IDM_LONG_ENABLE     2030
#define IDM_LONG_80         2031
#define IDM_LONG_85         2032

// Full Battery Alert Submenu
#define IDM_FULL_ENABLE     2040
#define IDM_FULL_95         2041
#define IDM_FULL_100        2042

#define IDM_AUTOSTART       2060
#define IDM_EXIT            2062

static const WCHAR *CLASS_NAME = L"CoreMonitor_Class";
static const WCHAR *DETAILS_CLASS_NAME = L"CoreMonitor_DetailsClass";
static const WCHAR *MUTEX_NAME = L"Global\\CoreMonitor_Mutex";

static HWND s_hWnd = NULL;
static HWND s_hDetailsWnd = NULL;
static BOOL s_detailsClassRegistered = FALSE;
static RECT s_closeButtonRect = {0};
static BOOL s_closeHover = FALSE;

static NOTIFYICONDATAW s_nid = {0};
static HICON s_hCurrentIcon = NULL;
static URXStatus s_currentStatus = {0};
static UINT s_uTaskbarRestartMsg = 0;

static float s_breathPhase = 0.0f;
static BOOL s_isAnimating = FALSE;
static HDEVNOTIFY s_hDevNotify = NULL;
static DWORD s_lastKeyboardInputTick = 0;
static DWORD s_lastRadioQueryTick = 0;
static DWORD s_lastManualToastTick = 0;
static BOOL s_isResumingFromSuspend = FALSE;
static HANDLE s_cachedUrxDevice1 = NULL;
static HANDLE s_cachedUrxDevice2 = NULL;
static HANDLE s_cachedNonUrxKeyboardDevice = NULL;

typedef enum {
    APPMODE_DEFAULT = 0,
    APPMODE_ALLOWDARK = 1,
    APPMODE_FORCEDARK = 2,
    APPMODE_FORCELIGHT = 3,
    APPMODE_MAX = 4
} PreferredAppMode;

typedef PreferredAppMode (WINAPI *pfnSetPreferredAppMode_t)(PreferredAppMode);
typedef BOOL (WINAPI *pfnAllowDarkModeForWindow_t)(HWND, BOOL);
typedef BOOL (WINAPI *pfnAllowDarkModeForApp_t)(BOOL);
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

        if (s_pfnSetPreferredAppMode) {
            s_pfnSetPreferredAppMode(APPMODE_ALLOWDARK);
        } else {
            pfnAllowDarkModeForApp_t pfnAllowDarkModeForApp = (pfnAllowDarkModeForApp_t)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
            if (pfnAllowDarkModeForApp) {
                pfnAllowDarkModeForApp(TRUE);
            }
        }

        if (s_pfnFlushMenuThemes) {
            s_pfnFlushMenuThemes();
        }
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
    if (s_pfnShouldAppsUseDarkMode) {
        return s_pfnShouldAppsUseDarkMode();
    }
    HKEY hKey;
    DWORD val = 0;
    DWORD size = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        RegCloseKey(hKey);
    }
    return FALSE;
}

static void ApplyWindowTheme(HWND hWnd, BOOL isDark) {
    if (s_pfnAllowDarkModeForWindow) {
        s_pfnAllowDarkModeForWindow(hWnd, isDark);
    }
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

static void FormatRichTooltip(WCHAR *outTip, DWORD maxLen) {
    if (!outTip || maxLen == 0) return;
    WCHAR estBuf[64];
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
    if (effPercent < 0 && s_currentStatus.mode != MODE_DISCONNECTED) {
        effPercent = 81;
    }

    BatteryStats_FormatRemainingText(
        effPercent,
        s_currentStatus.isCharging,
        s_currentStatus.isSleeping,
        (s_currentStatus.mode == MODE_DISCONNECTED),
        estBuf, 64
    );

    if (s_currentStatus.mode == MODE_DISCONNECTED) {
        _snwprintf(outTip, maxLen, L"URX Core75: Disconnected");
    } else if (s_currentStatus.isSleeping) {
        _snwprintf(outTip, maxLen, L"URX Core75: %d%% - [Asleep - 2.4GHz]\n%s", effPercent, estBuf);
    } else if (s_currentStatus.isCharging) {
        _snwprintf(outTip, maxLen, L"URX Core75: %d%% - [%s - Charging]\n%s", effPercent, s_currentStatus.modeName, estBuf);
    } else {
        _snwprintf(outTip, maxLen, L"URX Core75: %d%% - [%s]\n%s", effPercent, s_currentStatus.modeName, estBuf);
    }
    outTip[maxLen - 1] = L'\0';
}

static void ClearTrayToast(void) {
    s_nid.uFlags = NIF_INFO;
    s_nid.szInfo[0] = L'\0';
    s_nid.szInfoTitle[0] = L'\0';
    s_nid.dwInfoFlags = 0;
    Shell_NotifyIconW(NIM_MODIFY, &s_nid);
}

static void ShowTrayToast(const WCHAR *title, const WCHAR *msg, DWORD flags) {
    s_lastManualToastTick = GetTickCount();
    s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
    wcsncpy(s_nid.szInfoTitle, title, 63);
    s_nid.szInfoTitle[63] = L'\0';
    wcsncpy(s_nid.szInfo, msg, 255);
    s_nid.szInfo[255] = L'\0';
    s_nid.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &s_nid);
}

static void UpdateTrayIcon(void) {
    BOOL lightTaskbar = IconGen_IsTaskbarLight();
    BOOL isDisconnected = (s_currentStatus.mode == MODE_DISCONNECTED);
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
    if (effPercent < 0 && !isDisconnected) {
        effPercent = 81;
    }

    HICON hNewIcon = IconGen_CreateBatteryIcon(
        effPercent,
        s_currentStatus.isCharging,
        s_breathPhase,
        s_currentStatus.isSleeping,
        isDisconnected,
        g_Settings.centerStyle,
        lightTaskbar
    );

    if (hNewIcon) {
        s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        s_nid.hIcon = hNewIcon;
        FormatRichTooltip(s_nid.szTip, 128);
        s_nid.szTip[127] = L'\0';

        Shell_NotifyIconW(NIM_MODIFY, &s_nid);

        if (s_hCurrentIcon) {
            DestroyIcon(s_hCurrentIcon);
        }
        s_hCurrentIcon = hNewIcon;

        if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
            SendMessageW(s_hDetailsWnd, WM_SETICON, ICON_BIG, (LPARAM)s_hCurrentIcon);
            SendMessageW(s_hDetailsWnd, WM_SETICON, ICON_SMALL, (LPARAM)s_hCurrentIcon);
        }
    }
}

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
            if (s_pfnGetDpiForWindow) {
                dpi = s_pfnGetDpiForWindow(hWnd);
            }
            if (dpi <= 0) {
                dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            }
            if (dpi <= 0) dpi = 96;
            #define SC(val) MulDiv((val), dpi, 96)

            HFONT hFontTitle = CreateFontW(-SC(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hFontSub = CreateFontW(-SC(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hFontSecHdr = CreateFontW(-SC(10), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hFontRow = CreateFontW(-SC(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hFontBadge = CreateFontW(-SC(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hFontBtn = CreateFontW(-SC(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            SetBkMode(memDC, TRANSPARENT);

            int pad = SC(16);
            int cardW = width - (pad * 2);
            int curY = SC(14);

            // Title & Subtitle
            SelectObject(memDC, hFontTitle);
            SetTextColor(memDC, colTextPri);
            TextOutW(memDC, pad, curY, L"URX Core75", 10);

            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, colTextSec);
            TextOutW(memDC, pad, curY + SC(20), L"Mechanical Keyboard Telemetry \x2022 v1.3.0", 38);

            // Live status & battery badge
            int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
            if (effPercent < 0 && s_currentStatus.mode != MODE_DISCONNECTED) {
                effPercent = 81;
            }
            WCHAR badgeText[32];
            COLORREF colBadgeBg, colBadgeBdr, colBadgeText;
            if (s_currentStatus.mode == MODE_DISCONNECTED) {
                wcscpy(badgeText, L"Offline");
                colBadgeBg = isDark ? RGB(50, 25, 25) : RGB(254, 226, 226);
                colBadgeBdr = isDark ? RGB(100, 45, 45) : RGB(248, 113, 113);
                colBadgeText = isDark ? RGB(248, 113, 113) : RGB(185, 28, 28);
            } else if (s_currentStatus.isSleeping) {
                _snwprintf(badgeText, 32, L"%d%% - Asleep", effPercent);
                colBadgeBg = isDark ? RGB(20, 45, 65) : RGB(224, 242, 254);
                colBadgeBdr = isDark ? RGB(40, 90, 130) : RGB(56, 189, 248);
                colBadgeText = isDark ? RGB(56, 189, 248) : RGB(3, 105, 161);
            } else if (s_currentStatus.isCharging) {
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

            SelectObject(memDC, hFontBadge);
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

            // Data string formatting
            WCHAR estBuf[64];
            BatteryStats_FormatRemainingText(
                effPercent,
                s_currentStatus.isCharging,
                s_currentStatus.isSleeping,
                (s_currentStatus.mode == MODE_DISCONNECTED),
                estBuf, 64
            );

            double dRate = BatteryStats_GetDischargeRatePerHour();
            double cRate = BatteryStats_GetChargeRatePerHour();

            double endHours = 0.0;
            WCHAR enduranceStr[64];
            if (BatteryStats_GetEstimatedEnduranceHours(&endHours)) {
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
            _snwprintf(dRateStr, 32, L"~%.1f%% / hour", dRate);
            _snwprintf(cRateStr, 32, L"~%.1f%% / hour", cRate);

            // Card 1: Battery & Runtime
            int card1H = SC(92);
            DrawCardPanel(memDC, pad, curY, cardW, card1H, colCardBg, colBorder, SC(8));
            SelectObject(memDC, hFontSecHdr);
            SetTextColor(memDC, colHeader);
            TextOutW(memDC, pad + SC(12), curY + SC(8), L"BATTERY & RUNTIME", 17);

            int innerY = curY + SC(28);
            int rowH = SC(19);
            int innerW = cardW - SC(24);
            int innerX = pad + SC(12);

            WCHAR modeDisplay[64];
            wcsncpy(modeDisplay, s_currentStatus.modeName, 63);
            modeDisplay[63] = L'\0';

            DrawCardRow(memDC, innerX, innerY + (rowH * 0), innerW, rowH, L"Connection Mode", modeDisplay, colTextSec, colTextPri, hFontRow);
            DrawCardRow(memDC, innerX, innerY + (rowH * 1), innerW, rowH, L"Estimated Remaining", estBuf, colTextSec, colTextPri, hFontRow);
            DrawCardRow(memDC, innerX, innerY + (rowH * 2), innerW, rowH, L"Full Endurance", enduranceStr, colTextSec, colTextPri, hFontRow);

            curY += card1H + SC(10);

            // Card 2: Consumption Rates
            int card2H = SC(92);
            DrawCardPanel(memDC, pad, curY, cardW, card2H, colCardBg, colBorder, SC(8));
            SelectObject(memDC, hFontSecHdr);
            SetTextColor(memDC, colHeader);
            TextOutW(memDC, pad + SC(12), curY + SC(8), L"CONSUMPTION RATES", 17);

            innerY = curY + SC(28);
            DrawCardRow(memDC, innerX, innerY + (rowH * 0), innerW, rowH, L"Discharge Rate", dRateStr, colTextSec, colTextPri, hFontRow);
            DrawCardRow(memDC, innerX, innerY + (rowH * 1), innerW, rowH, L"Charge Rate", cRateStr, colTextSec, colTextPri, hFontRow);

            BOOL isFiltering = BatteryMonitor_IsFilteringTransient();
            BOOL isCalibrated = BatteryStats_IsCalibrated();
            const WCHAR *telemStatus = isFiltering ? L"Stabilizing (ADC filter)"
                                                   : (isCalibrated ? L"Calibrated (Profiling)" : L"Calibrating (Learning)");
            COLORREF colTelem = isFiltering ? (isDark ? RGB(251, 191, 36) : RGB(217, 119, 6))
                                            : (isCalibrated ? colAccent : colMuted);

            DrawCardRow(memDC, innerX, innerY + (rowH * 2), innerW, rowH, L"Telemetry Status",
                        telemStatus, colTextSec, colTelem, hFontRow);

            curY += card2H + SC(10);

            // Card 3: Alert Configurations
            int card3H = SC(112);
            DrawCardPanel(memDC, pad, curY, cardW, card3H, colCardBg, colBorder, SC(8));
            SelectObject(memDC, hFontSecHdr);
            SetTextColor(memDC, colHeader);
            TextOutW(memDC, pad + SC(12), curY + SC(8), L"ALERT CONFIGURATIONS", 20);

            innerY = curY + SC(28);
            WCHAR alertLabel[64];
            _snwprintf(alertLabel, 64, L"Low Battery Alert (<=%d%%)", g_Settings.lowAlertThreshold);
            DrawCardRow(memDC, innerX, innerY + (rowH * 0), innerW, rowH, alertLabel,
                        g_Settings.lowBatteryAlert ? L"Enabled" : L"Disabled",
                        colTextSec, g_Settings.lowBatteryAlert ? colAccent : colMuted, hFontRow);

            _snwprintf(alertLabel, 64, L"Longevity Alert (%d%%)", g_Settings.longevityAlertThreshold);
            DrawCardRow(memDC, innerX, innerY + (rowH * 1), innerW, rowH, alertLabel,
                        g_Settings.longevityAlert ? L"Enabled" : L"Disabled",
                        colTextSec, g_Settings.longevityAlert ? colAccent : colMuted, hFontRow);

            _snwprintf(alertLabel, 64, L"Full Battery Alert (%d%%)", g_Settings.highAlertThreshold);
            DrawCardRow(memDC, innerX, innerY + (rowH * 2), innerW, rowH, alertLabel,
                        g_Settings.highBatteryAlert ? L"Enabled" : L"Disabled",
                        colTextSec, g_Settings.highBatteryAlert ? colAccent : colMuted, hFontRow);

            DrawCardRow(memDC, innerX, innerY + (rowH * 3), innerW, rowH, L"Start with Windows",
                        g_Settings.startWithWindows ? L"Enabled" : L"Disabled",
                        colTextSec, g_Settings.startWithWindows ? colAccent : colMuted, hFontRow);

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
            SelectObject(memDC, hFontBtn);
            SetTextColor(memDC, colBtnText);
            RECT textRect = s_closeButtonRect;
            OffsetRect(&textRect, 0, -SC(1));
            DrawTextW(memDC, L"Close", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            #undef SC

            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

            // Cleanup GDI objects
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            DeleteObject(hFontTitle);
            DeleteObject(hFontSub);
            DeleteObject(hFontSecHdr);
            DeleteObject(hFontRow);
            DeleteObject(hFontBadge);
            DeleteObject(hFontBtn);

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
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void ShowDetailsDialog(HWND hWnd) {
    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
        ShowWindow(s_hDetailsWnd, SW_RESTORE);
        SetWindowPos(s_hDetailsWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
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

    HDC hdc = GetDC(NULL);
    int dpi = 96;
    if (s_pfnGetDpiForWindow && s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
        dpi = s_pfnGetDpiForWindow(s_hDetailsWnd);
    }
    if (dpi <= 0) {
        dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    }
    ReleaseDC(NULL, hdc);
    if (dpi <= 0) dpi = 96;

    int clientW = MulDiv(400, dpi, 96);
    int clientH = MulDiv(432, dpi, 96);

    RECT rc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, 0);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int posX = workArea.left + (workArea.right - workArea.left - winW) / 2;
    int posY = workArea.top + (workArea.bottom - workArea.top - winH) / 2;

    s_hDetailsWnd = CreateWindowExW(
        0,
        DETAILS_CLASS_NAME,
        L"Core Monitor - Battery & Device Statistics",
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        posX, posY, winW, winH,
        hWnd, NULL, hInstance, NULL
    );

    if (s_hDetailsWnd) {
        if (s_hCurrentIcon) {
            SendMessageW(s_hDetailsWnd, WM_SETICON, ICON_BIG, (LPARAM)s_hCurrentIcon);
            SendMessageW(s_hDetailsWnd, WM_SETICON, ICON_SMALL, (LPARAM)s_hCurrentIcon);
        }
        BOOL isDark = IsAppInDarkMode();
        ApplyWindowTheme(s_hDetailsWnd, isDark);
        ShowWindow(s_hDetailsWnd, SW_SHOW);
        UpdateWindow(s_hDetailsWnd);
        SetForegroundWindow(s_hDetailsWnd);
    }
}

static void PollAndUpdate(void) {
    DWORD now = GetTickCount();
    int sleepMin = BatteryMonitor_GetSleepTimerMinutes();
    DWORD sleepMs = (DWORD)sleepMin * 60 * 1000;

    DWORD idleMs = (s_lastKeyboardInputTick != 0) ? (DWORD)(now - s_lastKeyboardInputTick) : (sleepMs + 1);
    BOOL isSleeping = FALSE;
    BOOL allowWirelessQuery = FALSE;

    if (s_lastRadioQueryTick == 0) {
        // Initial one-shot probe on startup or explicit manual refresh
        allowWirelessQuery = TRUE;
    } else if (s_currentStatus.mode == MODE_2P4G) {
        if (idleMs >= sleepMs) {
            // No keystrokes for firmware sleep timeout: keyboard is asleep!
            isSleeping = TRUE;
            allowWirelessQuery = FALSE; // STRICT: Zero over-the-air packets while asleep!
        } else {
            // Keyboard is active (user typed recently).
            // Normal 5-minute interval: typing in last 60s and >= 5 minutes since last successful query
            if ((DWORD)(now - s_lastKeyboardInputTick) < 60000 && (DWORD)(now - s_lastRadioQueryTick) >= 300000) {
                allowWirelessQuery = TRUE;
            }
        }
    } else if (s_currentStatus.mode == MODE_WIRED) {
        allowWirelessQuery = TRUE;
    }

    BOOL liveQuerySuccess = BatteryMonitor_Poll(&s_currentStatus, allowWirelessQuery);

    if (allowWirelessQuery && s_currentStatus.mode == MODE_2P4G) {
        // Record query attempt timestamp (prevents repeated timeout hanging if keyboard is switched off - Issue 4)
        s_lastRadioQueryTick = now;
        if (liveQuerySuccess && s_lastKeyboardInputTick == 0) {
            s_lastKeyboardInputTick = now;
        }
    } else if (s_currentStatus.mode == MODE_WIRED) {
        s_lastRadioQueryTick = now;
        s_lastKeyboardInputTick = now;
    }

    // Set status text and states for 2.4G
    if (s_currentStatus.mode == MODE_2P4G) {
        if (!liveQuerySuccess && (idleMs >= sleepMs || s_lastKeyboardInputTick == 0)) {
            isSleeping = TRUE;
        } else if (liveQuerySuccess) {
            isSleeping = FALSE;
        }
        s_currentStatus.isSleeping = isSleeping;
        int effBatt = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
        if (effBatt < 0) effBatt = 81;
        s_currentStatus.batteryPercent = effBatt;
        s_currentStatus.lastKnownPercent = effBatt;

        if (isSleeping) {
            wcscpy(s_currentStatus.stateName, L"Asleep");
            _snwprintf(s_currentStatus.statusText, 128, L"URX Core75: %d%% - [Asleep - 2.4GHz]", effBatt);
        } else {
            wcscpy(s_currentStatus.stateName, L"Active");
            _snwprintf(s_currentStatus.statusText, 128, L"URX Core75: %d%% - [2.4GHz Wireless]", effBatt);
        }
    }

    BOOL isDisconnected = (s_currentStatus.mode == MODE_DISCONNECTED);
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
    if (effPercent < 0 && !isDisconnected) {
        effPercent = 81;
    }

    // Record battery observation in statistics engine
    BatteryStats_Record(effPercent, s_currentStatus.isCharging, s_currentStatus.isSleeping, isDisconnected);

    WCHAR toastBuf[256];

    // 1. Low battery alert (configurable threshold: <= lowAlertThreshold while discharging)
    if (g_Settings.lowBatteryAlert &&
        s_currentStatus.batteryPercent > 0 &&
        s_currentStatus.batteryPercent <= g_Settings.lowAlertThreshold &&
        !s_currentStatus.isCharging &&
        !isDisconnected) {

        if (!g_Settings.lowAlertShownForDischarge) {
            _snwprintf(toastBuf, 255, L"Battery is down to %d%%. Connect the USB cable to charge.", s_currentStatus.batteryPercent);
            ShowTrayToast(L"URX Core75 - Low Battery Alert", toastBuf, NIIF_WARNING);
            g_Settings.lowAlertShownForDischarge = TRUE;
        }
    } else if (s_currentStatus.batteryPercent > (g_Settings.lowAlertThreshold + 5) || s_currentStatus.isCharging) {
        g_Settings.lowAlertShownForDischarge = FALSE;
    }

    // 2. Battery longevity alert (configurable threshold: >= longevityAlertThreshold while charging)
    if (g_Settings.longevityAlert &&
        s_currentStatus.batteryPercent >= g_Settings.longevityAlertThreshold &&
        (s_currentStatus.batteryPercent < g_Settings.highAlertThreshold || !g_Settings.highBatteryAlert) &&
        s_currentStatus.isCharging &&
        !isDisconnected) {

        if (!g_Settings.longevityAlertShownForCharge) {
            _snwprintf(toastBuf, 255, L"Battery reached %d%%. Disconnect charger to protect lithium longevity.", s_currentStatus.batteryPercent);
            ShowTrayToast(L"URX Core75 - Battery Longevity Alert", toastBuf, NIIF_INFO);
            g_Settings.longevityAlertShownForCharge = TRUE;
        }
    } else if (s_currentStatus.batteryPercent < (g_Settings.longevityAlertThreshold - 5) || !s_currentStatus.isCharging) {
        g_Settings.longevityAlertShownForCharge = FALSE;
    }

    // 3. High battery alert (configurable threshold: >= highAlertThreshold while charging)
    if (g_Settings.highBatteryAlert &&
        s_currentStatus.batteryPercent >= g_Settings.highAlertThreshold &&
        s_currentStatus.isCharging &&
        !isDisconnected) {

        if (!g_Settings.highAlertShownForCharge) {
            _snwprintf(toastBuf, 255, L"Battery reached %d%%. Fully charged and ready for wireless use.", s_currentStatus.batteryPercent);
            ShowTrayToast(L"URX Core75 - High Battery Alert", toastBuf, NIIF_INFO);
            g_Settings.highAlertShownForCharge = TRUE;
        }
    } else if (s_currentStatus.batteryPercent < (g_Settings.highAlertThreshold - 5) || !s_currentStatus.isCharging) {
        g_Settings.highAlertShownForCharge = FALSE;
    }

    // Manage breathing animation timer:
    // Strictly active ONLY during charging! (0.0% CPU when discharging)
    if (s_currentStatus.isCharging) {
        if (!s_isAnimating) {
            s_isAnimating = TRUE;
            SetTimer(s_hWnd, IDT_ANIM_TIMER, 40, NULL); // ~25 FPS
        }
    } else {
        if (s_isAnimating) {
            s_isAnimating = FALSE;
            KillTimer(s_hWnd, IDT_ANIM_TIMER);
            s_breathPhase = 0.0f;
        }
    }

    // Gentle polling: poll every 60s (eliminates radio congestion, avoids typing hiccups, lets keyboard sleep deeply)
    SetTimer(s_hWnd, IDT_POLL_TIMER, 60000, NULL);

    UpdateTrayIcon();

    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
        InvalidateRect(s_hDetailsWnd, NULL, TRUE);
    }

    // Ultra-low resource footprint: trim working set back to OS cache
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

static void ShowContextMenu(HWND hWnd) {
    if (s_pfnSetPreferredAppMode) {
        s_pfnSetPreferredAppMode(APPMODE_ALLOWDARK);
    }
    if (s_pfnAllowDarkModeForWindow) {
        s_pfnAllowDarkModeForWindow(hWnd, TRUE);
    }
    if (s_pfnFlushMenuThemes) {
        s_pfnFlushMenuThemes();
    }

    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Header title
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_TITLE, L"Core Monitor");

    // Live status line
    WCHAR statusLine[128];
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
    if (effPercent < 0 && s_currentStatus.mode != MODE_DISCONNECTED) {
        effPercent = 81;
    }

    if (s_currentStatus.mode == MODE_DISCONNECTED) {
        _snwprintf(statusLine, sizeof(statusLine) / sizeof(WCHAR), L"Status: Disconnected");
    } else if (s_currentStatus.isSleeping) {
        _snwprintf(statusLine, sizeof(statusLine) / sizeof(WCHAR), L"Status: %d%% - [Asleep - 2.4GHz]", effPercent);
    } else if (s_currentStatus.isCharging) {
        _snwprintf(statusLine, sizeof(statusLine) / sizeof(WCHAR), L"Status: %d%% - [%s - Charging]", effPercent, s_currentStatus.modeName);
    } else {
        _snwprintf(statusLine, sizeof(statusLine) / sizeof(WCHAR), L"Status: %d%% - [%s]", effPercent, s_currentStatus.modeName);
    }
    statusLine[(sizeof(statusLine) / sizeof(WCHAR)) - 1] = L'\0';
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_INFO, statusLine);

    // Battery remaining estimation line
    WCHAR estLine[128];
    WCHAR estBuf[64];
    BatteryStats_FormatRemainingText(
        effPercent,
        s_currentStatus.isCharging,
        s_currentStatus.isSleeping,
        (s_currentStatus.mode == MODE_DISCONNECTED),
        estBuf, 64
    );
    _snwprintf(estLine, sizeof(estLine) / sizeof(WCHAR), L"Estimate: %s", estBuf);
    estLine[(sizeof(estLine) / sizeof(WCHAR)) - 1] = L'\0';
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_EST, estLine);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh Now");
    AppendMenuW(hMenu, MF_STRING, IDM_DETAILS, L"Battery Details && Statistics...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // 1. Configure Icon expandable submenu
    HMENU hIconMenu = CreatePopupMenu();
    AppendMenuW(hIconMenu, (g_Settings.centerStyle == ICON_CENTER_RING_ONLY ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_STYLE_RING_ONLY, L"Show Ring-Only");
    AppendMenuW(hIconMenu, (g_Settings.centerStyle == ICON_CENTER_PERCENT ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_STYLE_PERCENT, L"Show Number");
    AppendMenuW(hIconMenu, (g_Settings.centerStyle == ICON_CENTER_KEYBOARD ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_STYLE_KEYBOARD, L"Show Keyboard");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hIconMenu, L"Configure Icon");

    // 2. Low Battery Alert submenu
    HMENU hLowMenu = CreatePopupMenu();
    AppendMenuW(hLowMenu, (g_Settings.lowBatteryAlert ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LOW_ENABLE, L"Enabled");
    AppendMenuW(hLowMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hLowMenu, (g_Settings.lowAlertThreshold == 10 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LOW_10, L"10%");
    AppendMenuW(hLowMenu, (g_Settings.lowAlertThreshold == 15 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LOW_15, L"15%");
    AppendMenuW(hLowMenu, (g_Settings.lowAlertThreshold == 20 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LOW_20, L"20%");
    WCHAR subMenuLabel[64];
    _snwprintf(subMenuLabel, 64, L"Low Battery Alert (<=%d%%)", g_Settings.lowAlertThreshold);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hLowMenu, subMenuLabel);

    // 3. Battery Longevity Alert submenu
    HMENU hLongMenu = CreatePopupMenu();
    AppendMenuW(hLongMenu, (g_Settings.longevityAlert ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LONG_ENABLE, L"Enabled");
    AppendMenuW(hLongMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hLongMenu, (g_Settings.longevityAlertThreshold == 80 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LONG_80, L"80%");
    AppendMenuW(hLongMenu, (g_Settings.longevityAlertThreshold == 85 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_LONG_85, L"85%");
    _snwprintf(subMenuLabel, 64, L"Battery Longevity Alert (%d%%)", g_Settings.longevityAlertThreshold);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hLongMenu, subMenuLabel);

    // 4. Full Battery Alert submenu
    HMENU hFullMenu = CreatePopupMenu();
    AppendMenuW(hFullMenu, (g_Settings.highBatteryAlert ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_FULL_ENABLE, L"Enabled");
    AppendMenuW(hFullMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFullMenu, (g_Settings.highAlertThreshold == 95 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_FULL_95, L"95%");
    AppendMenuW(hFullMenu, (g_Settings.highAlertThreshold == 100 ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
                IDM_FULL_100, L"100%");
    _snwprintf(subMenuLabel, 64, L"Full Battery Alert (%d%%)", g_Settings.highAlertThreshold);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFullMenu, subMenuLabel);

    // Run on startup toggle
    UINT autoFlags = MF_STRING;
    if (g_Settings.startWithWindows) {
        autoFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, autoFlags, IDM_AUTOSTART, L"Start with Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    // Ensure tray menu properly dismisses when clicking elsewhere
    SetForegroundWindow(hWnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, hWnd, NULL);
    PostMessageW(hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

#ifndef RIDI_DEVICENAME
#define RIDI_DEVICENAME 0x20000007
#endif

// Determines if raw input event originated from URX Core75 keyboard
// Uses dual-handle caching (primary NKRO + media/knob endpoints) for zero-overhead (<1ns) subsequent lookups
static BOOL IsURXKeyboardDevice(HANDLE hDevice) {
    if (!hDevice) return TRUE; // Safe fallback for synthesized/injected keystrokes
    if (hDevice == s_cachedUrxDevice1 || hDevice == s_cachedUrxDevice2) {
        return TRUE;
    }
    if (s_cachedNonUrxKeyboardDevice && hDevice == s_cachedNonUrxKeyboardDevice) {
        return FALSE;
    }

    char name[512] = {0};
    UINT size = sizeof(name);
    UINT ret = GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, name, &size);
    if (ret != (UINT)-1 && ret > 0) {
        if (strstr(name, "36b0") || strstr(name, "36B0")) {
            if (!s_cachedUrxDevice1) {
                s_cachedUrxDevice1 = hDevice;
            } else if (!s_cachedUrxDevice2 && hDevice != s_cachedUrxDevice1) {
                s_cachedUrxDevice2 = hDevice;
            }
            return TRUE;
        } else {
            s_cachedNonUrxKeyboardDevice = hDevice;
            return FALSE;
        }
    }
    return TRUE; // Safe fallback if query failed
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == s_uTaskbarRestartMsg && s_uTaskbarRestartMsg != 0) {
        // Explorer restarted: re-register icon
        Shell_NotifyIconW(NIM_ADD, &s_nid);
        UpdateTrayIcon();
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            // Register for system-wide raw keyboard input (background sink)
            RAWINPUTDEVICE rid;
            rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
            rid.usUsage = 0x06;     // HID_USAGE_GENERIC_KEYBOARD
            rid.dwFlags = RIDEV_INPUTSINK;
            rid.hwndTarget = hWnd;
            RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
            s_lastKeyboardInputTick = 0;
            s_lastRadioQueryTick = 0;

            // Register for hotplug / device change notifications
            DEV_BROADCAST_DEVICEINTERFACE_W filter = {0};
            filter.dbcc_size = sizeof(filter);
            filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            s_hDevNotify = RegisterDeviceNotificationW(hWnd, &filter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

            // Register for Session Lock / Unlock notifications
            if (s_pfnWTSRegisterSessionNotification) {
                s_isWtsRegistered = s_pfnWTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);
            }

            // Register for Screen / Display Off / On state notifications
            if (s_pfnRegisterPowerSettingNotification) {
                s_hPowerNotify = s_pfnRegisterPowerSettingNotification(hWnd, &GUID_CONSOLE_DISPLAY_STATE_M, DEVICE_NOTIFY_WINDOW_HANDLE);
            }

            // Regular polling interval: every 60 seconds
            SetTimer(hWnd, IDT_POLL_TIMER, 60000, NULL);
            break;
        }

        case WM_INPUT: {
            HRAWINPUT hRaw = (HRAWINPUT)lParam;
            RAWINPUT raw;
            UINT dwSize = sizeof(RAWINPUT);
            if (GetRawInputData(hRaw, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                if (raw.header.dwType == RIM_TYPEKEYBOARD) {
                    if (raw.data.keyboard.Message == WM_KEYDOWN || raw.data.keyboard.Message == WM_SYSKEYDOWN) {
                        // Multi-keyboard isolation: only track input originating from the URX Core75
                        if (IsURXKeyboardDevice(raw.header.hDevice)) {
                            s_lastKeyboardInputTick = GetTickCount();
                            BatteryMonitor_ResetFailures();
                            if ((s_currentStatus.isSleeping || s_lastRadioQueryTick == 0) && s_currentStatus.mode == MODE_2P4G) {
                                // Instantly wake up UI state to Active!
                                s_currentStatus.isSleeping = FALSE;
                                wcscpy(s_currentStatus.stateName, L"Active");
                                int effBatt = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;
                                if (effBatt < 0) effBatt = 81;
                                _snwprintf(s_currentStatus.statusText, 128, L"URX Core75: %d%% - [2.4GHz Wireless]", effBatt);
                                UpdateTrayIcon();
                                if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
                                    InvalidateRect(s_hDetailsWnd, NULL, TRUE);
                                }
                                // Physical keystroke confirms keyboard is awake: sync immediately (150ms)
                                s_lastRadioQueryTick = 0;
                                SetTimer(hWnd, IDT_POLL_TIMER, 150, NULL);
                            }
                        }
                    }
                }
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        case WM_TIMER: {
            if (wParam == IDT_POLL_TIMER) {
                s_isResumingFromSuspend = FALSE;
                PollAndUpdate();
            } else if (wParam == IDT_ANIM_TIMER) {
                // Smooth breathing phase progression (2.5 second breath period)
                s_breathPhase += 0.08f;
                if (s_breathPhase > 2.0f * PI) {
                    s_breathPhase -= 2.0f * PI;
                }
                UpdateTrayIcon();
            } else if (wParam == IDT_CLICK_TIMER) {
                KillTimer(hWnd, IDT_CLICK_TIMER);
                // User performed a genuine single left-click: trigger quick refresh & status balloon
                s_lastKeyboardInputTick = GetTickCount();
                s_lastRadioQueryTick = 0;
                PollAndUpdate();

                // Prevent notification spam from repeated clicks
                DWORD now = GetTickCount();
                if (now - s_lastManualToastTick >= TOAST_MANUAL_COOLDOWN_MS) {
                    FormatRichTooltip(s_nid.szInfo, 255);
                    ShowTrayToast(L"URX Core75 Status", s_nid.szInfo, NIIF_INFO);
                }
            }
            break;
        }

        case WM_DEVICECHANGE: {
            // Hardware connect/disconnect event occurred!
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
                s_cachedUrxDevice1 = NULL;
                s_cachedUrxDevice2 = NULL;
                s_cachedNonUrxKeyboardDevice = NULL;
                s_lastRadioQueryTick = 0; // Fresh hardware connection: query immediately (Issue 2)
                BatteryMonitor_InvalidateCache();
                // Brief 150ms pause to let Windows PnP finish enumeration, then poll
                SetTimer(hWnd, IDT_POLL_TIMER, 150, NULL);
            }
            break;
        }

        case WM_SETTINGCHANGE: {
            // Windows theme changed (light/dark mode switch) or DPI change
            if (s_pfnSetPreferredAppMode) {
                s_pfnSetPreferredAppMode(APPMODE_ALLOWDARK);
            }
            if (s_pfnFlushMenuThemes) {
                s_pfnFlushMenuThemes();
            }
            UpdateTrayIcon();
            if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
                BOOL isDark = IsAppInDarkMode();
                ApplyWindowTheme(s_hDetailsWnd, isDark);
                InvalidateRect(s_hDetailsWnd, NULL, TRUE);
            }
            break;
        }

        case WM_POWERBROADCAST: {
            if (wParam == PBT_APMSUSPEND) {
                // PC entering sleep: pause polling and invalidate cached device paths
                s_isResumingFromSuspend = TRUE;
                s_cachedUrxDevice1 = NULL;
                s_cachedUrxDevice2 = NULL;
                s_cachedNonUrxKeyboardDevice = NULL;
                KillTimer(hWnd, IDT_POLL_TIMER);
                BatteryStats_ResetSessionBaseline();
                BatteryMonitor_InvalidateCache();
            } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                // PC resumed from sleep:
                // Passive USB presence check will immediately show clean last known battery in Asleep state
                s_isResumingFromSuspend = TRUE;
                s_cachedUrxDevice1 = NULL;
                s_cachedUrxDevice2 = NULL;
                s_cachedNonUrxKeyboardDevice = NULL;
                s_lastKeyboardInputTick = 0;
                s_lastRadioQueryTick = 0; // Unqueried after resume: wake event or poll syncs true battery immediately (Issue 1)
                s_currentStatus.isSleeping = TRUE;
                wcscpy(s_currentStatus.stateName, L"Asleep");
                BatteryStats_ResetSessionBaseline();
                BatteryMonitor_ResetFailures();
                BatteryMonitor_InvalidateCache();
                if (!s_isSessionLocked && !s_isDisplayOff) {
                    SetTimer(hWnd, IDT_POLL_TIMER, 1500, NULL);
                }
            } else if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
                POWERBROADCAST_SETTING_M *pSetting = (POWERBROADCAST_SETTING_M*)lParam;
                if (IsEqualGUID(&pSetting->PowerSetting, &GUID_CONSOLE_DISPLAY_STATE_M)) {
                    DWORD displayState = 0;
                    memcpy(&displayState, pSetting->Data, (pSetting->DataLength < sizeof(DWORD)) ? pSetting->DataLength : sizeof(DWORD));
                    if (displayState == 0) {
                        // Display turned off: pause polling & radio queries
                        s_isDisplayOff = TRUE;
                        KillTimer(hWnd, IDT_POLL_TIMER);
                        BatteryMonitor_InvalidateCache();
                    } else if (displayState == 1 || displayState == 2) {
                        // Display powered back on or un-dimmed: resume polling
                        s_isDisplayOff = FALSE;
                        if (!s_isSessionLocked) {
                            s_lastKeyboardInputTick = 0;
                            s_lastRadioQueryTick = 0; // Issue 1
                            // If PC is resuming from actual sleep, preserve the USB stabilization window
                            if (!s_isResumingFromSuspend) {
                                BatteryMonitor_ResetFailures();
                                BatteryMonitor_InvalidateCache();
                                SetTimer(hWnd, IDT_POLL_TIMER, 500, NULL);
                            }
                        }
                    }
                }
            }
            break;
        }

        case WM_WTSSESSION_CHANGE: {
            if (wParam == WTS_SESSION_LOCK) {
                // Workstation locked: pause polling & radio queries
                s_isSessionLocked = TRUE;
                KillTimer(hWnd, IDT_POLL_TIMER);
                BatteryMonitor_InvalidateCache();
            } else if (wParam == WTS_SESSION_UNLOCK) {
                // Workstation unlocked: resume polling if display is also active
                s_isSessionLocked = FALSE;
                if (!s_isDisplayOff) {
                    s_lastKeyboardInputTick = 0;
                    s_lastRadioQueryTick = 0; // Issue 1
                    BatteryMonitor_ResetFailures();
                    BatteryMonitor_InvalidateCache();
                    SetTimer(hWnd, IDT_POLL_TIMER, 500, NULL);
                }
            }
            break;
        }

        case WM_TRAY_CALLBACK: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                KillTimer(hWnd, IDT_CLICK_TIMER);
                ShowContextMenu(hWnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                KillTimer(hWnd, IDT_CLICK_TIMER);
                ClearTrayToast();
                ShowDetailsDialog(hWnd);
            } else if (lParam == WM_LBUTTONUP) {
                // Defer single-click to avoid collision with double-click
                UINT dblClickTime = GetDoubleClickTime();
                if (dblClickTime < 180) dblClickTime = 180;
                if (dblClickTime > 400) dblClickTime = 400;
                SetTimer(hWnd, IDT_CLICK_TIMER, dblClickTime, NULL);
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDM_REFRESH:
                    s_lastKeyboardInputTick = GetTickCount();
                    s_lastRadioQueryTick = 0;
                    s_cachedUrxDevice1 = NULL;
                    s_cachedUrxDevice2 = NULL;
                    s_cachedNonUrxKeyboardDevice = NULL;
                    BatteryMonitor_InvalidateCache();
                    PollAndUpdate();
                    break;
                case IDM_DETAILS:
                    ClearTrayToast();
                    ShowDetailsDialog(hWnd);
                    break;
                case IDM_STYLE_RING_ONLY:
                    g_Settings.centerStyle = ICON_CENTER_RING_ONLY;
                    Settings_Save();
                    UpdateTrayIcon();
                    break;
                case IDM_STYLE_PERCENT:
                    g_Settings.centerStyle = ICON_CENTER_PERCENT;
                    Settings_Save();
                    UpdateTrayIcon();
                    break;
                case IDM_STYLE_KEYBOARD:
                    g_Settings.centerStyle = ICON_CENTER_KEYBOARD;
                    Settings_Save();
                    UpdateTrayIcon();
                    break;

                case IDM_LOW_ENABLE:
                    g_Settings.lowBatteryAlert = !g_Settings.lowBatteryAlert;
                    goto save_and_invalidate;
                case IDM_LOW_10:
                    g_Settings.lowAlertThreshold = 10;
                    goto save_and_invalidate;
                case IDM_LOW_15:
                    g_Settings.lowAlertThreshold = 15;
                    goto save_and_invalidate;
                case IDM_LOW_20:
                    g_Settings.lowAlertThreshold = 20;
                    goto save_and_invalidate;

                case IDM_LONG_ENABLE:
                    g_Settings.longevityAlert = !g_Settings.longevityAlert;
                    goto save_and_invalidate;
                case IDM_LONG_80:
                    g_Settings.longevityAlertThreshold = 80;
                    goto save_and_invalidate;
                case IDM_LONG_85:
                    g_Settings.longevityAlertThreshold = 85;
                    goto save_and_invalidate;

                case IDM_FULL_ENABLE:
                    g_Settings.highBatteryAlert = !g_Settings.highBatteryAlert;
                    goto save_and_invalidate;
                case IDM_FULL_95:
                    g_Settings.highAlertThreshold = 95;
                    goto save_and_invalidate;
                case IDM_FULL_100:
                    g_Settings.highAlertThreshold = 100;
                    goto save_and_invalidate;

                case IDM_AUTOSTART:
                    Settings_SetAutostart(!g_Settings.startWithWindows);
                    goto save_and_invalidate;

                save_and_invalidate:
                    Settings_Save();
                    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) InvalidateRect(s_hDetailsWnd, NULL, TRUE);
                    break;

                case IDM_EXIT:
                    DestroyWindow(hWnd);
                    break;
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
            KillTimer(hWnd, IDT_POLL_TIMER);
            KillTimer(hWnd, IDT_ANIM_TIMER);
            KillTimer(hWnd, IDT_CLICK_TIMER);

            if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
                DestroyWindow(s_hDetailsWnd);
                s_hDetailsWnd = NULL;
            }

            ClearTrayToast();
            // Remove tray icon
            Shell_NotifyIconW(NIM_DELETE, &s_nid);
            if (s_hCurrentIcon) {
                DestroyIcon(s_hCurrentIcon);
                s_hCurrentIcon = NULL;
            }

            BatteryStats_Save();
            BatteryMonitor_Cleanup();
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

    // 1. Ensure thread is bound to user's interactive "Default" desktop
    HDESK hDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    if (hDesk) {
        SetThreadDesktop(hDesk);
    }

    // Initialize System-Adaptive Dark Mode for Win32 menus
    InitDarkMode();

    // Initialize Session Lock and Screen Off power hooks
    InitPowerAndSessionHooks();

    // Set Process AppUserModelID for modern Windows Action Center toasts
    if (s_pfnSetCurrentProcessExplicitAppUserModelID) {
        s_pfnSetCurrentProcessExplicitAppUserModelID(L"CoreMonitor");
    }

    // 2. Single Instance Guard (Wake existing instance to front)
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindowW(CLASS_NAME, L"Core Monitor");
        if (hExistingWnd) {
            PostMessageW(hExistingWnd, WM_COMMAND, IDM_DETAILS, 0);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0; // Instance already running
    }

    // 3. Register Explorer Taskbar Restart Message
    s_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // 4. Load User Preferences and Battery Statistics
    Settings_Load();
    BatteryStats_Init();

    // 5. Initialize Hardware Subsystems
    if (!BatteryMonitor_Init()) {
        MessageBoxW(NULL, L"Failed to initialize Windows HID subsystems.", L"Core Monitor Error", MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 6. Register Hidden Message Window Class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        CloseHandle(hMutex);
        return 1;
    }

    // 7. Create Hidden Top-Level Window (WS_POPUP, invisible)
    s_hWnd = CreateWindowExW(0, CLASS_NAME, L"Core Monitor", WS_POPUP, 0, 0, 0, 0,
                             NULL, NULL, hInstance, NULL);
    if (!s_hWnd) {
        CloseHandle(hMutex);
        return 1;
    }

    if (s_pfnAllowDarkModeForWindow && s_hWnd) {
        s_pfnAllowDarkModeForWindow(s_hWnd, TRUE);
    }

    // 8. Setup Taskbar Notification Icon
    s_nid.cbSize = sizeof(NOTIFYICONDATAW);
    s_nid.hWnd = s_hWnd;
    s_nid.uID = 1001;
    s_nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    s_nid.uCallbackMessage = WM_TRAY_CALLBACK;
    wcsncpy(s_nid.szTip, L"Core Monitor", 127);

    // Initial Hardware Poll
    PollAndUpdate();

    // Register notification icon with taskbar
    Shell_NotifyIconW(NIM_ADD, &s_nid);
    UpdateTrayIcon();

    // 9. Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(hMutex);
    return (int)msg.wParam;
}
