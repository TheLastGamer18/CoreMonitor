# Core Monitor v1.5.0

Version 1.5.0 introduces concurrent wireless charging detection in 2.4GHz mode without requiring firmware modifications, introduces a calibrated 5-minute trend-detection engine with smart unplug timeouts, and streamlines the core monitoring architecture to reduce binary footprint down to 59.3 KB.

## What's New

### 2.4GHz Concurrent Wireless & Charging Detection
- **Stock Firmware Compatible Charging Engine**: CoreMonitor now accurately detects when the keyboard is connected to USB-C charging power while remaining in 2.4GHz wireless mode—requiring **zero firmware modifications**. The tray status, rich tooltip, and menus dynamically reflect `[2.4GHz Wireless - Charging]` without misrepresenting your active connection mode.
- **Calibrated 5-Minute Polling Filter**: Optimized upward battery filtering with a 2-cycle confirmation threshold, slashing upward charge recognition latency by 66% on standard 5-minute poll intervals while preserving complete immunity against single-sample ADC noise.
- **Smart Unplug Detection (3-Cycle Timeout)**: Integrated an automatic 15-minute (3-cycle) flat-percentage timeout. If charging power is disconnected, CoreMonitor automatically disengages the charging indicator without waiting 40–90+ minutes for a battery drop.
- **Dynamic Tray Animation**: The tray battery icon displays the green charging arc and breathing pulse whenever charging is active, even while actively typing wirelessly over the 2.4GHz dongle.
- **Zero RF Packet Overhead**: Charging inference operates strictly on verified battery delta trends from the existing single query payload. Zero additional over-the-air packets are transmitted, completely preserving wireless sleep synchronization and battery health.
- **Real-Time Charge Rate Learning**: The statistics engine now records battery charging intervals in 2.4GHz mode, calculating time-to-full and time-to-85% predictions rather than treating voltage gains as discharge noise.

### Architecture Optimization & Bloat Removal
- **Consolidated Query Pipeline**: Merged and streamlined VIA battery telemetry queries into a single, unified function, eliminating redundant wrapper layers and dead parameter passing.
- **Streamlined Status Pipeline**: Eliminated write-only legacy struct members (`statusText`, `stateName`, `sleepTimerMinutes`) from the hardware telemetry struct, removing dozens of redundant string formatting calls per polling cycle.
- **Dead Code Cleanup**: Purged obsolete consecutive-failure counters, dead dynamic library function pointers, and unused standard library includes across the codebase.
- **Reduced Binary Footprint**: Cleaned compiler dependencies and optimized memory layout, reducing the standalone executable size down to **59.3 KB** (59,392 bytes) with 0.0% CPU overhead while on battery.

## Installation
1. Download `CoreMonitor.exe`.
2. Place it anywhere and run. No installation required.
