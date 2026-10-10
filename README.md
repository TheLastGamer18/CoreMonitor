# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the URX Core75 Keyboard.

---

## About This Project

This application is currently developed as a personal project. It was created out of a desire for a genuinely minimal, highly optimized utility that tracks just what is needed—battery levels and charging state—without the heavy background runtimes, multi-megabyte memory footprints, or bloated multi-device software suites typical of commercial peripheral managers.

Written in pure Win32 C, Core Monitor compiles to a single standalone executable of approximately 47 KB and runs in roughly 1.8 MB of RAM with 0.0% idle CPU usage.

---

## Features

### 1. Multi-Mode Connection Tracking
* 2.4 GHz Wireless (USB Dongle): Live battery telemetry queried through the keyboard's QMK VIA raw HID endpoint.
* Wired USB: Automatic detection with charging status indication.
* Bluetooth (BLE): Native Windows Bluetooth Battery Service (BAS) reading via SetupAPI.

### 2. Clean System Tray Indicator
* Anti-Aliased Ring Gauge: Circular battery level meter with smooth rounded endcaps and dark circular background track.
* 75% Mechanical Keyboard Glyph: Crisp central silhouette depicting a 75% keyboard layout with function row, main key block, spacebar, and arrow cluster notch.
* Color Thresholds:
  * Mint / Green: Above 40% (or actively charging)
  * Warm Amber: 21% to 40%
  * Alert Red: 20% and below
  * Dimmed Gray: Asleep or disconnected (preserves last known battery reading)
* Active Charging Animation: Gentle breathing glow on the outer ring and lightning badge while charging. The animation timer terminates completely when not charging for true 0.0% CPU usage.
* Display DPI Aware: Automatically scales to native small icon sizes (16x16, 20x20, 24x24, 32x32) across different Windows display scaling factors (100%, 125%, 150%, 200%).
* Taskbar Theme Adaptive: Automatically detects Windows Dark and Light taskbar settings.

### 3. Battery Statistics and Runtime Estimation
* Discharge Rate Tracking: Records battery drop intervals to calculate a rolling average discharge rate (percent per hour).
* Remaining Runtime Estimate: Calculates estimated battery life remaining (for example, "~19h 40m remaining").
* Charging Duration Estimate: Calculates estimated time required to reach 85% and 100% full charge.
* Persistent Statistics: Stores learned discharge and charge rates in the Windows Registry so estimates remain accurate across computer restarts.

### 4. Smart Battery Notifications
* Low Battery Alert (<= 20%): Balloon warning reminding you to connect the charging cable.
* Battery Longevity Alert (>= 85%): Notification when charging crosses 85%, allowing you to unplug and preserve lithium-ion battery health.
* High Battery Alert (100%): Notification when charging reaches 100%, indicating the battery is fully charged and ready for wireless use.
* Debounced Delivery: Alerts fire only once per cycle with built-in hysteresis to prevent repetitive notification spam.

### 5. Hardware Safety and Radio Conservation
* Read-Only Queries: Uses only the read-only QMK VIA Custom Value command (`[0x08, 0x00, 0x07]`). It performs zero EEPROM writes and zero flash sector modifications, ensuring no risk to keyboard firmware or saved keymaps.
* Non-Exclusive Sharing: Device handles are opened with `FILE_SHARE_READ | FILE_SHARE_WRITE`, allowing full coexistence with the VIA Web Configurator, keyboard flashing utilities, and games.
* Cached Device Paths: Eliminates repeated USB bus enumerations during routine polling cycles.
* Sleep Mode & Suspension Backoff: Automatic power-event awareness (`WM_POWERBROADCAST` and `PBT_APMSUSPEND`), ensuring zero radio queries while the computer is sleeping.
* Session Lock & Display-Off Awareness: Integrates with Windows Terminal Services (`WTSRegisterSessionNotification`) and display power notifications (`GUID_CONSOLE_DISPLAY_STATE`). Polling is completely suspended whenever the workstation is locked (`Win+L`) or displays enter standby, eliminating unnecessary radio wakeups when away from the computer.

### Windows Integration
* Rich Tooltip: Multi-line hover tooltip showing device name, battery percentage, active connection mode, state, and runtime estimation.
* Context Menu:
  * Live status summary and runtime estimate
  * Manual refresh option
  * Battery Details & Statistics window (accessible via menu or double-clicking tray icon)
  * Icon style toggle (keyboard glyph vs. numeric percentage)
  * Low battery alert toggle
  * Battery longevity alert toggle
  * High battery alert toggle
  * System-Adaptive Theme: Context menu seamlessly renders in native Windows Dark Mode or Light Mode matching your system personalization preferences.
  * Start with Windows toggle
  * Clean application exit
* Seamless Startup: Stores autostart preference cleanly in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Runs silently into the system tray without console windows or startup delays.

---

## Specifications

* Binary Size: ~47 KB (single standalone .exe, zero external dependencies)
* Memory Usage: ~1.8 MB to 2.4 MB RAM (automatic working set trimming)
* Idle CPU: 0.0%
* Supported OS: Windows 10, Windows 11 (64-bit and 32-bit)
* Supported Keyboard Hardware: URX Core75 (Wired PID: 0x3130, 2.4G PID: 0x3002, VID: 0x36B0) and compatible QMK/VIA HID devices

---

## Getting Started

There are two ways to obtain and run Core Monitor:

### Method 1: Download Pre-built Binary (Recommended)
1. Go to the [Releases](../../releases) tab on the GitHub repository.
2. Download the latest `CoreMonitor.exe`.
3. Place `CoreMonitor.exe` in any directory of your choice and double-click to run. No installation, runtime packages, or admin privileges are required.

### Method 2: Building from Source
If you prefer compiling the source code yourself:

#### Prerequisites
* MinGW-w64 with GCC (`gcc`) and GNU Windres (`windres`)

#### Build Script
Run the included batch script from the repository root:
```cmd
build.bat
```

#### Manual Compilation
```cmd
windres -i res\resource.rc -o res\resource.o -I res
gcc -Os -s -Wall -fno-asynchronous-unwind-tables -fno-ident -mwindows -static-libgcc -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi -lm
```

---

## Usage

1. Launch `CoreMonitor.exe`.
2. The application will immediately place a dynamic battery icon in your system tray next to the Windows clock.
3. Left-click the tray icon to trigger an instant hardware poll and view a status balloon.
4. Right-click the tray icon to view live statistics, toggle notification preferences, enable start-with-Windows, or open the detailed telemetry window.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for full details.
