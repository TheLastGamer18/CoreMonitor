# Core Monitor

An ultra-lightweight, dedicated Windows system tray battery monitor built specifically for the URX Core75 Keyboard.

No heavy runtimes, no background bloat. Compiles to a single standalone executable under 55 KB that uses ~1.8 MB RAM and 0.0% CPU.

---

## Screenshots

| In Taskbar (with Battery Number) | In Taskbar (with Keyboard Icon) |
| :---: | :---: |
| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558443068854636585/image.png?ex=6acb74b6&is=6aca2336&hm=70aa990fef6f02312158ab48f6f6594584cc5351e9a540023f8147f6bf85923d&" height="180" style="vertical-align: middle;"> | <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558446150942457927/image.png?ex=6acb7794&is=6aca2614&hm=d53d346b9a168fe4e025fa4eddfc474df3dc9f0302f9fb181703a2ee5c5e360e&" height="180" style="vertical-align: middle;"> |


| Right-Click Menu | Battery Details & Statistics Menu |
| :---: | :---: |
| <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558443069366603848/image.png?ex=6acb74b6&is=6aca2336&hm=63317789c9a134ab0eece2afd94238058f72f063a0415ac1166ac02762e6c9a7&" height="450" style="vertical-align: middle;"> | <img src="https://cdn.discordapp.com/attachments/799204313498648599/1558443069798355014/image.png?ex=6acb74b6&is=6aca2336&hm=e71ed5b3be81b51de869a2b46373e93a61e78c39f956db826f7a46217af93090&" height="450" style="vertical-align: middle;"> |

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
gcc -Os -s -Wall -fmerge-all-constants -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -mwindows -static-libgcc -Wl,--gc-sections -Wl,--file-alignment,512 -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi -lm
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.
