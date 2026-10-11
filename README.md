# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the URX Core75 Keyboard.

No heavy runtimes, no background bloat. Compiles to a single standalone executable under 60 KB that uses ~300 KB RAM and 0.0% CPU.

---

## Screenshots

| In Taskbar (with Battery Number) | In Taskbar (with Keyboard Icon) |
| :---: | :---: |
| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558443068854636585/image.png?ex=6acb74b6&is=6aca2336&hm=70aa990fef6f02312158ab48f6f6594584cc5351e9a540023f8147f6bf85923d&" height="180" style="vertical-align: middle;"> | <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558446150942457927/image.png?ex=6acb7794&is=6aca2614&hm=d53d346b9a168fe4e025fa4eddfc474df3dc9f0302f9fb181703a2ee5c5e360e&" height="180" style="vertical-align: middle;"> |


| Right-Click Menu | Battery Details & Statistics Menu |
| :---: | :---: |
| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558443069366603848/image.png?ex=6acb74b6&is=6aca2336&hm=63317789c9a134ab0eece2afd94238058f72f063a0415ac1166ac02762e6c9a7&" height="450" style="vertical-align: middle;"> | <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558815792622080140/image.png?ex=6acccfd6&is=6acb7e56&hm=12adffb817e175dfcc9ab4fa687990930a5be1800ce82b5d862cded95151a21d&" height="450" style="vertical-align: middle;"> |

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
gcc -Os -s -Wall -flto -fmerge-all-constants -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -fomit-frame-pointer -falign-functions=1 -falign-jumps=1 -falign-loops=1 -mwindows -static-libgcc -Wl,--gc-sections -Wl,--file-alignment,512 -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.
