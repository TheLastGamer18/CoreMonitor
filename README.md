# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the URX Core75 Keyboard.

No heavy runtimes, no background bloat. Compiles to a single standalone executable under 50 KB that uses ~1.8 MB RAM and 0.0% CPU.

---

## Highlights

- **Connection Modes:** Tracks 2.4 GHz wireless (USB dongle), wired USB, and Bluetooth (BLE).
- **Tray Icon:**
  - Circular battery ring with an active pulse when charging.
  - Option to show either the keyboard icon or a live percentage number.
  - Automatically matches Windows dark and light themes.
- **Battery Alerts:**
  - **Low (<= 20%):** Warns you when it is time to plug in.
  - **Longevity (85%):** Notifies you to unplug and preserve battery health.
  - **Full (100%):** Confirms the keyboard is fully charged.
  - Alerts trigger once per cycle to prevent notification spam.
- **Runtime Stats:** Learns your battery discharge rate and estimates remaining usage hours.
- **Hardware Safe:**
  - Read-only memory queries (no EEPROM writes, zero flash wear).
  - Pauses polling automatically when Windows is locked or monitors go to sleep.
  - Works alongside VIA Web Configurator without device conflicts.

---

## Usage

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
gcc -Os -s -Wall -fno-asynchronous-unwind-tables -fno-ident -mwindows -static-libgcc -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi -lm
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.
