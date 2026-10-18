# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the URX Core75 Keyboard.

No heavy runtimes, no background bloat. Compiles to a single standalone executable under 60 KB that uses ~300 KB RAM and 0.0% CPU.

---

## Screenshots

| In Taskbar (with Ring Only) | In Taskbar (with Battery Number) | In Taskbar (with Keyboard Icon) |
| :---: | :---: | :---: |
| <img height="180" alt="image" src="https://github.com/user-attachments/assets/1b0be342-5dc8-424e-9d38-387121788646" /> | <img height="180" alt="image" src="https://github.com/user-attachments/assets/f0e70039-785b-4bc5-a2cb-a86dcc09a74b" /> | <img height="180" alt="image" src="https://github.com/user-attachments/assets/cbfe381f-a2f5-4f51-9a33-95f2824fa35b" /> |


| Right-Click Menu | Battery Details & Statistics Menu |
| :---: | :---: |
| <img height="450" alt="image" src="https://github.com/user-attachments/assets/25c419fe-1005-4bc4-b542-42fd7a1a3168" /> | <img height="450" alt="image" src="https://github.com/user-attachments/assets/f463581e-d0f3-4186-a870-00946a6db243" /> |

---

## Highlights

- **Connection Modes:** Tracks 2.4 GHz wireless (USB dongle), wired USB, and Bluetooth (BLE).
- **Firmware-Synced Sleep Protection:**
  - Synchronizes with keyboard sleep countdown via passive keystroke timing with zero radio traffic.
  - Upon waking up, instantly updates the tray icon.
- **Tray Icon & Configuration:**
  - Dynamic circular battery ring with active breathing pulse when charging.
  - Expandable **Configure Icon** menu: choose between **Ring-Only**, **Show Number**, or **Show Keyboard**.
  - Automatically matches Windows dark and light themes.
- **Configurable Battery Alerts:**
  - **Low Battery Alert:** Selectable threshold (10%, 15%, or 20%) to warn you when battery runs low.
  - **Longevity Alert:** Selectable threshold (80% or 85%) to preserve battery health.
  - **Full Battery Alert:** Selectable threshold (95% or 100%) to confirm full charge.
- **Runtime Stats & Details:** Learns discharge rates, estimates remaining hours, and displays live firmware telemetry.
- **Hardware Safe & Ultra-Lean:**
  - Strictly read-only memory queries with zero heap allocations during polling.
  - Pauses polling automatically when Windows is locked or monitors go to sleep.
  - Works alongside VIA Web Configurator without device conflicts.

---

## Download and Run (Recommended)

1. Download `CoreMonitor.exe` from [Releases](../../releases).
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

Or compile manually:
```cmd
windres -i res\resource.rc -o res\resource.o -I res
gcc -Os -s -Wall -flto -fno-math-errno -fno-trapping-math -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -fomit-frame-pointer -falign-functions=1 -falign-jumps=1 -falign-loops=1 -mwindows -static-libgcc -Wl,--gc-sections -Wl,--file-alignment,512 -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.
