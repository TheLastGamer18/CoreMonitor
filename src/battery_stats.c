#include "battery_stats.h"
#include <stdio.h>
#include <time.h>

#define STATS_REG_KEY L"Software\\CoreMonitor\\Stats"
#define CURRENT_STATS_VERSION 2

// Default initial rates:
// Typical 3000mAh-4000mAh wireless keyboard:
// Discharge: ~0.50% per hour (~200 hours baseline before calibration)
// Charge: ~35.0% per hour (~3 hours full charge)
#define DEFAULT_DISCHARGE_RATE_PER_HOUR 0.50
#define DEFAULT_CHARGE_RATE_PER_HOUR    35.0

static double s_dischargeRatePerHour = DEFAULT_DISCHARGE_RATE_PER_HOUR;
static double s_chargeRatePerHour    = DEFAULT_CHARGE_RATE_PER_HOUR;

static BOOL s_hasLearnedDischarge = FALSE;
static BOOL s_hasLearnedCharge    = FALSE;

// Active Monitored Time State
static DWORD s_lastMonitoredTick  = 0;   // GetTickCount() timestamp of last processed poll
static int   s_baselinePercent    = -1;  // Starting percentage for current accumulation window
static BOOL  s_lastWasCharging    = FALSE;
static DWORD s_accumDischargeSec  = 0;   // Accumulated active monitored seconds discharging
static DWORD s_accumChargeSec     = 0;   // Accumulated active monitored seconds charging

void BatteryStats_Init(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STATS_REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVersion = 0;
        DWORD dwSize = sizeof(DWORD);

        // Check stats format version. If version < 2, ignore old rates
        // (which were corrupted by the sleep reset bug) to start with fresh, accurate calibration.
        if (RegQueryValueExW(hKey, L"StatsVersion", NULL, NULL, (LPBYTE)&dwVersion, &dwSize) == ERROR_SUCCESS && dwVersion >= CURRENT_STATS_VERSION) {
            DWORD dwDischargeMilli = 0;
            DWORD dwChargeMilli = 0;
            DWORD dwAccumSec = 0;
            DWORD dwBasePercent = 0;

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"DischargeMilliRate", NULL, NULL, (LPBYTE)&dwDischargeMilli, &dwSize) == ERROR_SUCCESS) {
                if (dwDischargeMilli >= 10 && dwDischargeMilli <= 50000) { // 0.01% to 50% per hr
                    s_dischargeRatePerHour = (double)dwDischargeMilli / 1000.0;
                    s_hasLearnedDischarge = TRUE;
                }
            }

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"ChargeMilliRate", NULL, NULL, (LPBYTE)&dwChargeMilli, &dwSize) == ERROR_SUCCESS) {
                if (dwChargeMilli >= 5000 && dwChargeMilli <= 100000) { // 5% to 100% per hr
                    s_chargeRatePerHour = (double)dwChargeMilli / 1000.0;
                    s_hasLearnedCharge = TRUE;
                }
            }

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"AccumDischargeSec", NULL, NULL, (LPBYTE)&dwAccumSec, &dwSize) == ERROR_SUCCESS) {
                if (dwAccumSec <= 86400 * 30) { // Max 30 days of monitored accumulation safety cap
                    s_accumDischargeSec = dwAccumSec;
                }
            }

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"BaselinePercent", NULL, NULL, (LPBYTE)&dwBasePercent, &dwSize) == ERROR_SUCCESS) {
                if (dwBasePercent <= 100) {
                    s_baselinePercent = (int)dwBasePercent;
                }
            }
        }

        RegCloseKey(hKey);
    }
}

void BatteryStats_Save(void) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, STATS_REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD dwVersion = CURRENT_STATS_VERSION;
        DWORD dwDischargeMilli = (DWORD)(s_dischargeRatePerHour * 1000.0 + 0.5);
        DWORD dwChargeMilli = (DWORD)(s_chargeRatePerHour * 1000.0 + 0.5);
        DWORD dwAccumSec = s_accumDischargeSec;
        DWORD dwBasePercent = (s_baselinePercent >= 0) ? (DWORD)s_baselinePercent : 0xFFFFFFFF;

        RegSetValueExW(hKey, L"StatsVersion", 0, REG_DWORD, (const BYTE*)&dwVersion, sizeof(DWORD));
        RegSetValueExW(hKey, L"DischargeMilliRate", 0, REG_DWORD, (const BYTE*)&dwDischargeMilli, sizeof(DWORD));
        RegSetValueExW(hKey, L"ChargeMilliRate", 0, REG_DWORD, (const BYTE*)&dwChargeMilli, sizeof(DWORD));
        RegSetValueExW(hKey, L"AccumDischargeSec", 0, REG_DWORD, (const BYTE*)&dwAccumSec, sizeof(DWORD));
        RegSetValueExW(hKey, L"BaselinePercent", 0, REG_DWORD, (const BYTE*)&dwBasePercent, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

void BatteryStats_ResetSessionBaseline(void) {
    // Re-anchor the poll tick so PC suspend/sleep duration is never counted
    // while safely preserving accumulated monitored discharge seconds and baseline percent.
    s_lastMonitoredTick = 0;
}

void BatteryStats_Record(int batteryPercent, BOOL isCharging, BOOL isSleeping, BOOL isDisconnected) {
    (void)isSleeping;

    if (isDisconnected || batteryPercent < 0 || batteryPercent > 100) {
        // Disconnected: suspend active tick tracking so offline time is not counted
        s_lastMonitoredTick = 0;
        return;
    }

    DWORD nowTick = GetTickCount();

    // Initial baseline capture on first valid reading
    if (s_lastMonitoredTick == 0 || s_baselinePercent < 0) {
        s_baselinePercent = batteryPercent;
        s_accumDischargeSec = 0;
        s_accumChargeSec = 0;
        s_lastWasCharging = isCharging;
        s_lastMonitoredTick = nowTick;
        return;
    }

    // Measure delta time between active polling ticks
    DWORD deltaMs = nowTick - s_lastMonitoredTick;
    s_lastMonitoredTick = nowTick;
    DWORD deltaSec = deltaMs / 1000;

    // Active Monitored Time filter:
    // Only accumulate seconds if polling has been continuous (<= 120s between polls).
    // Gaps > 120s indicate PC sleep, screen off, or lock, and are strictly discarded.
    BOOL isContinuousMonitored = (deltaSec > 0 && deltaSec <= 120);

    // If state changed between charging and discharging, reset the window
    if (s_lastWasCharging != isCharging) {
        s_baselinePercent = batteryPercent;
        s_lastWasCharging = isCharging;
        s_accumDischargeSec = 0;
        s_accumChargeSec = 0;
        return;
    }

    if (!isCharging) {
        // DISCHARGING
        if (isContinuousMonitored) {
            s_accumDischargeSec += deltaSec;
        }

        if (batteryPercent < s_baselinePercent) {
            int deltaPercent = s_baselinePercent - batteryPercent;
            double hours = (double)s_accumDischargeSec / 3600.0;

            // Robust evaluation trigger:
            // 1. Require at least 2% drop OR
            // 2. Require at least 1% drop with >= 1.0 hour of active monitored observation.
            // This completely eliminates single-percentage quantization edge jitter.
            BOOL shouldUpdate = (deltaPercent >= 2) || (deltaPercent >= 1 && hours >= 1.0);

            if (shouldUpdate && hours > 0.05) {
                double instantRate = (double)deltaPercent / hours;

                // Sanity bounds: 0.01% to 20.0% per hour
                // (0.01%/hr = ~10,000 hrs / >1 year; 20.0%/hr = 5 hours heavy RGB use)
                if (instantRate >= 0.01 && instantRate <= 20.0) {
                    if (!s_hasLearnedDischarge) {
                        s_dischargeRatePerHour = instantRate;
                        s_hasLearnedDischarge = TRUE;
                    } else {
                        // Smooth exponential moving average: 80% history, 20% new observation
                        s_dischargeRatePerHour = 0.80 * s_dischargeRatePerHour + 0.20 * instantRate;
                    }
                    BatteryStats_Save();
                }

                s_baselinePercent = batteryPercent;
                s_accumDischargeSec = 0;
            }
        } else if (batteryPercent > s_baselinePercent) {
            // Voltage rebound or unflagged charge
            s_baselinePercent = batteryPercent;
        }
    } else {
        // CHARGING
        if (isContinuousMonitored) {
            s_accumChargeSec += deltaSec;
        }

        if (batteryPercent > s_baselinePercent) {
            int deltaPercent = batteryPercent - s_baselinePercent;
            double hours = (double)s_accumChargeSec / 3600.0;

            BOOL shouldUpdate = (deltaPercent >= 2) || (deltaPercent >= 1 && hours >= 0.25);

            if (shouldUpdate && hours > 0.05) {
                double instantRate = (double)deltaPercent / hours;

                // Sanity bounds: 5% to 100% per hour
                if (instantRate >= 5.0 && instantRate <= 100.0) {
                    if (!s_hasLearnedCharge) {
                        s_chargeRatePerHour = instantRate;
                        s_hasLearnedCharge = TRUE;
                    } else {
                        s_chargeRatePerHour = 0.75 * s_chargeRatePerHour + 0.25 * instantRate;
                    }
                    BatteryStats_Save();
                }

                s_baselinePercent = batteryPercent;
                s_accumChargeSec = 0;
            }
        } else if (batteryPercent < s_baselinePercent) {
            s_baselinePercent = batteryPercent;
        }
    }
}

BOOL BatteryStats_GetDischargeTimeRemaining(int currentPercent, int *outMinutes) {
    if (!outMinutes || currentPercent <= 0 || s_dischargeRatePerHour <= 0.005) {
        if (outMinutes) *outMinutes = 0;
        return FALSE;
    }

    double hours = (double)currentPercent / s_dischargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    if (mins < 1) mins = 1;
    *outMinutes = mins;
    return TRUE;
}

static BOOL CalcTimeToPercent(int currentPercent, int targetPercent, int *outMinutes) {
    if (!outMinutes) return FALSE;
    if (currentPercent >= targetPercent) {
        *outMinutes = 0;
        return TRUE;
    }
    if (s_chargeRatePerHour <= 1.0) {
        *outMinutes = 0;
        return FALSE;
    }
    double hours = (double)(targetPercent - currentPercent) / s_chargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    *outMinutes = (mins < 1) ? 1 : mins;
    return TRUE;
}

BOOL BatteryStats_GetChargeTimeTo85(int currentPercent, int *outMinutes) {
    return CalcTimeToPercent(currentPercent, 85, outMinutes);
}

BOOL BatteryStats_GetChargeTimeToFull(int currentPercent, int *outMinutes) {
    return CalcTimeToPercent(currentPercent, 100, outMinutes);
}

static void FormatMinutesToText(int totalMinutes, WCHAR *buf, DWORD maxLen) {
    if (totalMinutes <= 0) {
        wcsncpy(buf, L"0m", maxLen);
        return;
    }

    int hours = totalMinutes / 60;
    int mins = totalMinutes % 60;

    if (hours >= 24) {
        int days = hours / 24;
        int remHours = hours % 24;
        if (remHours > 0) {
            _snwprintf(buf, maxLen, L"~%dd %dh", days, remHours);
        } else {
            _snwprintf(buf, maxLen, L"~%dd", days);
        }
    } else if (hours > 0) {
        if (mins > 0) {
            _snwprintf(buf, maxLen, L"~%dh %dm", hours, mins);
        } else {
            _snwprintf(buf, maxLen, L"~%dh", hours);
        }
    } else {
        _snwprintf(buf, maxLen, L"~%dm", mins);
    }
    buf[maxLen - 1] = L'\0';
}

void BatteryStats_FormatRemainingText(
    int currentPercent,
    BOOL isCharging,
    BOOL isSleeping,
    BOOL isDisconnected,
    WCHAR *outBuffer,
    DWORD bufferCharCount
) {
    if (!outBuffer || bufferCharCount == 0) return;
    outBuffer[0] = L'\0';

    if (isDisconnected || currentPercent < 0) {
        wcsncpy(outBuffer, L"Offline", bufferCharCount - 1);
        outBuffer[bufferCharCount - 1] = L'\0';
        return;
    }

    if (isCharging) {
        if (currentPercent >= 100) {
            wcsncpy(outBuffer, L"Fully Charged", bufferCharCount - 1);
        } else if (currentPercent >= 85) {
            int minsFull = 0;
            if (BatteryStats_GetChargeTimeToFull(currentPercent, &minsFull)) {
                WCHAR timeBuf[32];
                FormatMinutesToText(minsFull, timeBuf, 32);
                _snwprintf(outBuffer, bufferCharCount, L"%s to full", timeBuf);
            } else {
                wcsncpy(outBuffer, L"Charging...", bufferCharCount - 1);
            }
        } else {
            int mins85 = 0;
            if (BatteryStats_GetChargeTimeTo85(currentPercent, &mins85)) {
                WCHAR timeBuf[32];
                FormatMinutesToText(mins85, timeBuf, 32);
                _snwprintf(outBuffer, bufferCharCount, L"%s to 85%%", timeBuf);
            } else {
                wcsncpy(outBuffer, L"Charging...", bufferCharCount - 1);
            }
        }
        outBuffer[bufferCharCount - 1] = L'\0';
        return;
    }

    // Discharging
    int minsRem = 0;
    if (BatteryStats_GetDischargeTimeRemaining(currentPercent, &minsRem)) {
        WCHAR timeBuf[32];
        FormatMinutesToText(minsRem, timeBuf, 32);
        if (isSleeping) {
            _snwprintf(outBuffer, bufferCharCount, L"%s left (asleep)", timeBuf);
        } else {
            _snwprintf(outBuffer, bufferCharCount, L"%s remaining", timeBuf);
        }
    } else {
        if (isSleeping) {
            wcsncpy(outBuffer, L"Asleep", bufferCharCount - 1);
        } else {
            wcsncpy(outBuffer, L"Calculating...", bufferCharCount - 1);
        }
    }
    outBuffer[bufferCharCount - 1] = L'\0';
}

double BatteryStats_GetDischargeRatePerHour(void) {
    return s_dischargeRatePerHour;
}

double BatteryStats_GetChargeRatePerHour(void) {
    return s_chargeRatePerHour;
}

BOOL BatteryStats_GetEstimatedEnduranceHours(double *outHours) {
    if (!outHours) return FALSE;
    if (s_dischargeRatePerHour <= 0.005) {
        *outHours = 0.0;
        return FALSE;
    }
    *outHours = 100.0 / s_dischargeRatePerHour;
    return TRUE;
}

BOOL BatteryStats_IsCalibrated(void) {
    return s_hasLearnedDischarge;
}
