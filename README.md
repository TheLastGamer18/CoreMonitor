# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the **URX Core75 Keyboard** and **URX Core M1 Mouse**.

No heavy runtimes, no background bloat. Standalone executables compile to ~62–68 KB that use ~300 KB RAM and 0.0% CPU.

---

## 3 Modular Executables

- **`CoreMonitor-Core75.exe`**: Dedicated battery monitor for the URX Core75 keyboard (~63 KB).
- **`CoreMonitor-CoreM1.exe`**: Dedicated battery monitor for the URX Core M1 mouse (~64 KB).
- **`CoreMonitor.exe`**: Combined dual monitor managing both devices side-by-side in your system tray (~68 KB).

---

## Screenshots

| In Taskbar (with Ring Only) | In Taskbar (with Battery Percentage) | In Taskbar (with Device Icon) |
| :---: | :---: | :---: |
| <img height="180" alt="image" src="https://github.com/user-attachments/assets/1b0be342-5dc8-424e-9d38-387121788646" /> | <img height="180" alt="image" src="https://github.com/user-attachments/assets/f0e70039-785b-4bc5-a2cb-a86dcc09a74b" /> | <img height="180" alt="image" src="https://github.com/user-attachments/assets/cbfe381f-a2f5-4f51-9a33-95f2824fa35b" /> |


| Right-Click Menu | Battery Details & Statistics Menu |
| :---: | :---: |
| <img height="450" alt="image" src="https://github.com/user-attachments/assets/25c419fe-1005-4bc4-b542-42fd7a1a3168" /> | <img height="450" alt="image" src="https://github.com/user-attachments/assets/f463581e-d0f3-4186-a870-00946a6db243" /> |

---

## Highlights

- **Connection Modes:** Tracks 2.4 GHz wireless (USB dongle), wired USB, and Bluetooth (BLE).
- **Firmware-Synced Sleep Protection & Instant Wake:**
  - Synchronizes with keyboard and mouse sleep countdowns via passive Windows raw input tracking with zero radio traffic.
  - Raw input hooks filter exclusively for URX Core75 and Core M1 hardware interfaces, ignoring all third-party devices.
  - Automatically dims the tray icon when the device enters sleep; wakes up instantly upon the next keystroke or mouse movement.
- **Tray Icon & Configuration:**
  - Dynamic circular battery ring with active breathing pulse when charging.
  - Pixel-perfect center icon choices: **Ring-Only**, **Show Battery Percentage**, or device icon (**Show Keyboard Icon** / **Show Mouse Icon**).
  - Clean stadium-capsule mouse glyph with floating scroll wheel and detailed mechanical keyboard glyph.
  - Automatically matches Windows dark and light taskbar themes.
- **Configurable Battery Alerts:**
  - **Low Battery Alert:** Selectable threshold (10%, 15%, or 20%) to warn you when battery runs low.
  - **Longevity Alert:** Selectable threshold (80% or 85%) to preserve battery health.
  - **Full Battery Alert:** Selectable threshold (95% or 100%) to confirm full charge.
- **Runtime Stats & Details:**
  - Displays battery percentage, discharge/charge trends, connection mode, estimated runtime remaining, and firmware info.
  - Live **Polling Rate** readout for the Core M1 mouse.
- **Hardware Safe & Ultra-Lean:**
  - Strictly read-only memory queries with zero heap allocations during active polling.
  - Pauses polling automatically when Windows is locked or monitors go to sleep.
  - Works alongside VIA Web Configurator without device conflicts.

---

## Download and Run (Recommended)

1. Download the executable of your choice (`CoreMonitor.exe` for both devices, or the standalone `CoreMonitor-Core75.exe` / `CoreMonitor-CoreM1.exe`). from [Releases](../../releases).
2. Run it. No installation or setup needed.
3. **Left-click** the tray icon for an instant battery status balloon.
4. **Right-click** to view device details, switch icon styles, change alert settings, or toggle Start with Windows.

---

## Build from Source

Requires MinGW-w64 (`gcc` and `windres`).

Run the build script:
```cmd
build.bat
```

The script automatically builds all 3 targets:
- `CoreMonitor-Core75.exe` (Core75 Keyboard Only)
- `CoreMonitor-CoreM1.exe` (Core M1 Mouse Only)
- `CoreMonitor.exe` (Combined Dual Monitor)

---

## License

MIT License. See [LICENSE](LICENSE) for details.
