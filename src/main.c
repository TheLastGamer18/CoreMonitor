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

#define WM_TRAY_CALLBACK (WM_APP + 100)
#define IDT_POLL_TIMER   1001
#define IDT_ANIM_TIMER   1002

#define IDM_STATUS_TITLE  2001
#define IDM_STATUS_INFO   2002
#define IDM_STATUS_EST    2003
#define IDM_REFRESH       2004
#define IDM_DETAILS       2005
#define IDM_TOGGLE_STYLE  2007
#define IDM_LOW_ALERT     2008
#define IDM_HIGH_ALERT    2009
#define IDM_AUTOSTART     2010
#define IDM_EXIT          2011

static const WCHAR *CLASS_NAME = L"CoreMonitor_Class";
static const WCHAR *MUTEX_NAME = L"Global\\CoreMonitor_Mutex";

static HWND s_hWnd = NULL;
static NOTIFYICONDATAW s_nid = {0};
static HICON s_hCurrentIcon = NULL;
static URXStatus s_currentStatus = {0};
static UINT s_uTaskbarRestartMsg = 0;

static float s_breathPhase = 0.0f;
static BOOL s_isAnimating = FALSE;
static HDEVNOTIFY s_hDevNotify = NULL;

static void FormatRichTooltip(WCHAR *outTip, DWORD maxLen) {
    WCHAR estBuf[64];
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;

    BatteryStats_FormatRemainingText(
        effPercent,
        s_currentStatus.isCharging,
        s_currentStatus.isSleeping,
        (s_currentStatus.mode == MODE_DISCONNECTED),
        estBuf, 64
    );

    if (s_currentStatus.mode == MODE_DISCONNECTED) {
        wcsncpy(outTip, L"URX Core75: Disconnected", maxLen - 1);
    } else if (s_currentStatus.isSleeping) {
        if (effPercent >= 0) {
            wsprintfW(outTip, L"URX Core75: %d%% (Asleep - 2.4GHz)\n%s", effPercent, estBuf);
        } else {
            wsprintfW(outTip, L"URX Core75: Asleep (2.4GHz)");
        }
    } else if (s_currentStatus.isCharging) {
        wsprintfW(outTip, L"URX Core75: %d%% [%s - Charging]\n%s", effPercent, s_currentStatus.modeName, estBuf);
    } else {
        wsprintfW(outTip, L"URX Core75: %d%% [%s]\n%s", effPercent, s_currentStatus.modeName, estBuf);
    }
    outTip[maxLen - 1] = L'\0';
}

static void UpdateTrayIcon(void) {
    BOOL lightTaskbar = IconGen_IsTaskbarLight();
    BOOL isDisconnected = (s_currentStatus.mode == MODE_DISCONNECTED);
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;

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

        Shell_NotifyIconW(NIM_MODIFY, &s_nid);

        if (s_hCurrentIcon) {
            DestroyIcon(s_hCurrentIcon);
        }
        s_hCurrentIcon = hNewIcon;
    }
}

static void ShowDetailsDialog(HWND hWnd) {
    WCHAR message[1024];
    WCHAR estBuf[64];
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;

    BatteryStats_FormatRemainingText(
        effPercent,
        s_currentStatus.isCharging,
        s_currentStatus.isSleeping,
        (s_currentStatus.mode == MODE_DISCONNECTED),
        estBuf, 64
    );

    double dRate = BatteryStats_GetDischargeRatePerHour();
    double cRate = BatteryStats_GetChargeRatePerHour();

    const WCHAR *connName = s_currentStatus.modeName;
    const WCHAR *stateStr = s_currentStatus.stateName;

    WCHAR battLevelStr[32];
    if (effPercent < 0) {
        wcscpy(battLevelStr, L"Offline / Disconnected");
    } else {
        _snwprintf(battLevelStr, sizeof(battLevelStr) / sizeof(WCHAR), L"%d%%", effPercent);
    }

    double endHours = 0.0;
    WCHAR enduranceStr[64];
    if (BatteryStats_GetEstimatedEnduranceHours(&endHours)) {
        if (endHours >= 24.0) {
            double days = endHours / 24.0;
            _snwprintf(enduranceStr, sizeof(enduranceStr) / sizeof(WCHAR), L"~%.0f hours (~%.1f days of usage)", endHours, days);
        } else {
            _snwprintf(enduranceStr, sizeof(enduranceStr) / sizeof(WCHAR), L"~%.1f hours of usage", endHours);
        }
    } else {
        wcscpy(enduranceStr, L"Calculating (learning typing usage)...");
    }

    _snwprintf(message, sizeof(message) / sizeof(WCHAR),
        L"URX Core75 Mechanical Keyboard\n"
        L"--------------------------------------------------\n"
        L"Battery Level:          %s\n"
        L"Current State:          %s\n"
        L"Connection Mode:    %s\n"
        L"Runtime Remaining:  %s\n"
        L"Full Endurance:         %s\n\n"
        L"Telemetry & Statistics:\n"
        L"  * Discharge Rate:     ~%.1f%% / hour\n"
        L"  * Charge Rate:         ~%.1f%% / hour\n"
        L"  * Hardware Protocol:  QMK VIA Raw HID (VID 0x36B0)\n"
        L"  * Query Safety:       Read-only memory query (Zero EEPROM wear)\n"
        L"  * Radio Conservation: Automatic sleep backoff (60s cycle)\n"
        L"--------------------------------------------------\n"
        L"Low Alert (<=20%%):    %s\n"
        L"High Alert (>85%%):    %s\n"
        L"Start with Windows:    %s",
        battLevelStr,
        stateStr,
        connName,
        estBuf,
        enduranceStr,
        dRate,
        cRate,
        g_Settings.lowBatteryAlert ? L"Enabled" : L"Disabled",
        g_Settings.highBatteryAlert ? L"Enabled" : L"Disabled",
        g_Settings.startWithWindows ? L"Enabled" : L"Disabled"
    );

    MessageBoxW(hWnd, message, L"Core Monitor - Device Statistics", MB_ICONINFORMATION | MB_OK);
}

static void PollAndUpdate(void) {
    BatteryMonitor_Poll(&s_currentStatus);

    BOOL isDisconnected = (s_currentStatus.mode == MODE_DISCONNECTED);
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;

    // Record battery observation in statistics engine
    BatteryStats_Record(effPercent, s_currentStatus.isCharging, s_currentStatus.isSleeping, isDisconnected);

    // 1. Low battery alert (threshold: <= 20% while discharging)
    if (g_Settings.lowBatteryAlert &&
        s_currentStatus.batteryPercent > 0 &&
        s_currentStatus.batteryPercent <= 20 &&
        !s_currentStatus.isCharging &&
        !isDisconnected) {

        if (!g_Settings.lowAlertShownForDischarge) {
            s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
            wcsncpy(s_nid.szInfoTitle, L"URX Core75 - Low Battery Alert", 63);
            wsprintfW(s_nid.szInfo, L"Battery is down to %d%%. Connect the USB cable to charge.", s_currentStatus.batteryPercent);
            s_nid.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIconW(NIM_MODIFY, &s_nid);
            g_Settings.lowAlertShownForDischarge = TRUE;
        }
    } else if (s_currentStatus.batteryPercent > 25 || s_currentStatus.isCharging) {
        g_Settings.lowAlertShownForDischarge = FALSE;
    }

    // 2. High battery alert (threshold: >= 85% while charging to preserve lithium lifespan)
    if (g_Settings.highBatteryAlert &&
        s_currentStatus.batteryPercent >= 85 &&
        s_currentStatus.isCharging &&
        !isDisconnected) {

        if (!g_Settings.highAlertShownForCharge) {
            s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
            wcsncpy(s_nid.szInfoTitle, L"URX Core75 - Battery Charged", 63);
            wsprintfW(s_nid.szInfo, L"Battery reached %d%%. Disconnect charger to protect lithium longevity.", s_currentStatus.batteryPercent);
            s_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &s_nid);
            g_Settings.highAlertShownForCharge = TRUE;
        }
    } else if (s_currentStatus.batteryPercent < 80 || !s_currentStatus.isCharging) {
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

    // Ultra-low resource footprint: trim working set back to OS cache
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

static void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Header title
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_TITLE, L"Core Monitor");

    // Live status line
    WCHAR statusLine[128];
    int effPercent = (s_currentStatus.batteryPercent >= 0) ? s_currentStatus.batteryPercent : s_currentStatus.lastKnownPercent;

    if (s_currentStatus.mode == MODE_DISCONNECTED) {
        wsprintfW(statusLine, L"Status: Disconnected");
    } else if (s_currentStatus.isSleeping) {
        if (effPercent >= 0) {
            wsprintfW(statusLine, L"Status: %d%% [Asleep - 2.4GHz]", effPercent);
        } else {
            wsprintfW(statusLine, L"Status: Asleep (2.4GHz)");
        }
    } else if (s_currentStatus.isCharging) {
        wsprintfW(statusLine, L"Status: %d%% [%s - Charging]", effPercent, s_currentStatus.modeName);
    } else {
        wsprintfW(statusLine, L"Status: %d%% [%s]", effPercent, s_currentStatus.modeName);
    }
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
    wsprintfW(estLine, L"Estimate: %s", estBuf);
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS_EST, estLine);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh Now");
    AppendMenuW(hMenu, MF_STRING, IDM_DETAILS, L"Battery Details && Statistics...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // Icon style toggle
    UINT styleFlags = MF_STRING;
    if (g_Settings.centerStyle == ICON_CENTER_PERCENT) {
        styleFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, styleFlags, IDM_TOGGLE_STYLE, L"Show Number in Icon");

    // Low battery alert toggle
    UINT lowFlags = MF_STRING;
    if (g_Settings.lowBatteryAlert) {
        lowFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, lowFlags, IDM_LOW_ALERT, L"Low Battery Alert (<=20%)");

    // High battery alert toggle
    UINT highFlags = MF_STRING;
    if (g_Settings.highBatteryAlert) {
        highFlags |= MF_CHECKED;
    }
    AppendMenuW(hMenu, highFlags, IDM_HIGH_ALERT, L"High Battery Alert (>85%)");

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

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == s_uTaskbarRestartMsg && s_uTaskbarRestartMsg != 0) {
        // Explorer restarted: re-register icon
        Shell_NotifyIconW(NIM_ADD, &s_nid);
        UpdateTrayIcon();
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            // Register for hotplug / device change notifications
            DEV_BROADCAST_DEVICEINTERFACE_W filter = {0};
            filter.dbcc_size = sizeof(filter);
            filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            s_hDevNotify = RegisterDeviceNotificationW(hWnd, &filter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

            // Regular polling interval: every 60 seconds
            SetTimer(hWnd, IDT_POLL_TIMER, 60000, NULL);
            break;
        }

        case WM_TIMER: {
            if (wParam == IDT_POLL_TIMER) {
                PollAndUpdate();
            } else if (wParam == IDT_ANIM_TIMER) {
                // Smooth breathing phase progression (2.5 second breath period)
                s_breathPhase += 0.08f;
                if (s_breathPhase > 2.0f * PI) {
                    s_breathPhase -= 2.0f * PI;
                }
                UpdateTrayIcon();
            }
            break;
        }

        case WM_DEVICECHANGE: {
            // Hardware connect/disconnect event occurred!
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
                BatteryMonitor_InvalidateCache();
                // Brief 150ms pause to let Windows PnP finish enumeration, then poll
                SetTimer(hWnd, IDT_POLL_TIMER, 150, NULL);
            }
            break;
        }

        case WM_SETTINGCHANGE: {
            // Windows theme changed (light/dark mode switch) or DPI change
            UpdateTrayIcon();
            break;
        }

        case WM_POWERBROADCAST: {
            if (wParam == PBT_APMSUSPEND) {
                // PC entering sleep: pause polling and invalidate cached device paths
                KillTimer(hWnd, IDT_POLL_TIMER);
                BatteryMonitor_InvalidateCache();
            } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                // PC resumed from sleep:
                // Give USB controllers and 2.4G dongles 2.5s to cleanly re-establish RF link
                BatteryMonitor_InvalidateCache();
                SetTimer(hWnd, IDT_POLL_TIMER, 2500, NULL);
            }
            break;
        }

        case WM_TRAY_CALLBACK: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowContextMenu(hWnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                ShowDetailsDialog(hWnd);
            } else if (lParam == WM_LBUTTONUP) {
                // Left click triggers quick refresh and shows status balloon
                PollAndUpdate();
                s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
                wcsncpy(s_nid.szInfoTitle, L"URX Core75 Status", 63);
                FormatRichTooltip(s_nid.szInfo, 255);
                s_nid.dwInfoFlags = NIIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &s_nid);
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDM_REFRESH:
                    BatteryMonitor_InvalidateCache();
                    PollAndUpdate();
                    break;
                case IDM_DETAILS:
                    ShowDetailsDialog(hWnd);
                    break;
                case IDM_TOGGLE_STYLE:
                    g_Settings.centerStyle = (g_Settings.centerStyle == ICON_CENTER_KEYBOARD)
                        ? ICON_CENTER_PERCENT : ICON_CENTER_KEYBOARD;
                    Settings_Save();
                    UpdateTrayIcon();
                    break;
                case IDM_LOW_ALERT:
                    g_Settings.lowBatteryAlert = !g_Settings.lowBatteryAlert;
                    Settings_Save();
                    break;
                case IDM_HIGH_ALERT:
                    g_Settings.highBatteryAlert = !g_Settings.highBatteryAlert;
                    Settings_Save();
                    break;
                case IDM_AUTOSTART:
                    Settings_SetAutostart(!g_Settings.startWithWindows);
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
            KillTimer(hWnd, IDT_POLL_TIMER);
            KillTimer(hWnd, IDT_ANIM_TIMER);

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

    // 2. Single Instance Guard
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
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

    // 8. Setup Taskbar Notification Icon
    s_nid.cbSize = sizeof(NOTIFYICONDATAW);
    s_nid.hWnd = s_hWnd;
    s_nid.uID = 1001;
    s_nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    s_nid.uCallbackMessage = WM_TRAY_CALLBACK;
    wcsncpy(s_nid.szTip, L"Core Monitor", 127);

    // Initial icon creation and registration
    s_hCurrentIcon = IconGen_CreateBatteryIcon(-1, FALSE, 0.0f, FALSE, TRUE, g_Settings.centerStyle, IconGen_IsTaskbarLight());
    s_nid.hIcon = s_hCurrentIcon;
    Shell_NotifyIconW(NIM_ADD, &s_nid);

    // Initial Hardware Poll
    PollAndUpdate();

    // 9. Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(hMutex);
    return (int)msg.wParam;
}
