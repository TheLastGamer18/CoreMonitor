# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the URX Core75 Keyboard.

No heavy runtimes, no background bloat. Compiles to a single standalone executable under 60 KB that uses ~300 KB RAM and 0.0% CPU.

---

## Screenshots

| In Taskbar (with Ring Only) | In Taskbar (with Battery Number) | In Taskbar (with Keyboard Icon) |
| :---: | :---: | :---: |
| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558822209286570095/image.png?ex=6accd5d0&is=6acb8450&hm=ac9982b68c292a2f5f869140b3a44ad13d8fe7bcbbd4604c045db305f50bf397&" height="180" style="vertical-align: middle;">| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558823037850361926/image.png?ex=6accd695&is=6acb8515&hm=5fe64e5f18864dda6e2931641bc17d52d905efa0e6b4670240b30defeb3458bb&" height="180" style="vertical-align: middle;"> | <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558823087712247878/image.png?ex=6accd6a1&is=6acb8521&hm=338ea460643c3ead4f2cb65acaeda8b1ce13236cf97a2aefecebf833672c3ac0&" height="180" style="vertical-align: middle;"> |


| Right-Click Menu | Battery Details & Statistics Menu |
| :---: | :---: |
| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558823423000707092/image.png?ex=6accd6f1&is=6acb8571&hm=636f096123acce5f4307537b3377aea28a2098f423cec8f779cac2e925d6400d&" height="450" style="vertical-align: middle;"> | <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558820445816356864/image.png?ex=6accd42b&is=6acb82ab&hm=667a12f63de493c2f1a0af515a0ec5ab9aa082553f16cf596400b45f39edef1c&" height="450" style="vertical-align: middle;"> |

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
