#include "battery_stats.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

#define STATS_REG_KEY L"Software\\CoreMonitor\\Stats"

// Default initial rates:
// Typical 3000mAh-4000mAh 75% keyboard:
// Discharge: ~2.0% per hour (~50 hours battery life)
// Charge: ~35.0% per hour (~3 hours full charge)
#define DEFAULT_DISCHARGE_RATE_PER_HOUR 2.0
#define DEFAULT_CHARGE_RATE_PER_HOUR    35.0

static double s_dischargeRatePerHour = DEFAULT_DISCHARGE_RATE_PER_HOUR;
static double s_chargeRatePerHour    = DEFAULT_CHARGE_RATE_PER_HOUR;

static int s_lastPercent = -1;
static BOOL s_lastWasCharging = FALSE;
static time_t s_lastTimestamp = 0;
static BOOL s_hasLearnedDischarge = FALSE;
static BOOL s_hasLearnedCharge = FALSE;

void BatteryStats_Init(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STATS_REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwDischargeMilli = 0;
        DWORD dwChargeMilli = 0;
        DWORD dwSize = sizeof(DWORD);

        if (RegQueryValueExW(hKey, L"DischargeMilliRate", NULL, NULL, (LPBYTE)&dwDischargeMilli, &dwSize) == ERROR_SUCCESS) {
            if (dwDischargeMilli >= 100 && dwDischargeMilli <= 50000) { // 0.1% to 50% per hr
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

        RegCloseKey(hKey);
    }
}

void BatteryStats_Save(void) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, STATS_REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD dwDischargeMilli = (DWORD)(s_dischargeRatePerHour * 1000.0 + 0.5);
        DWORD dwChargeMilli = (DWORD)(s_chargeRatePerHour * 1000.0 + 0.5);

        RegSetValueExW(hKey, L"DischargeMilliRate", 0, REG_DWORD, (const BYTE*)&dwDischargeMilli, sizeof(DWORD));
        RegSetValueExW(hKey, L"ChargeMilliRate", 0, REG_DWORD, (const BYTE*)&dwChargeMilli, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

void BatteryStats_Record(int batteryPercent, BOOL isCharging, BOOL isSleeping, BOOL isDisconnected) {
    if (isDisconnected || batteryPercent < 0 || batteryPercent > 100) {
        return;
    }

    time_t now = time(NULL);

    if (s_lastPercent < 0 || s_lastTimestamp == 0) {
        s_lastPercent = batteryPercent;
        s_lastWasCharging = isCharging;
        s_lastTimestamp = now;
        return;
    }

    // If state changed between charging and discharging, reset the baseline observation
    if (s_lastWasCharging != isCharging) {
        s_lastPercent = batteryPercent;
        s_lastWasCharging = isCharging;
        s_lastTimestamp = now;
        return;
    }

    double deltaSec = difftime(now, s_lastTimestamp);

    // Require at least 60 seconds of elapsed time between rate evaluations
    if (deltaSec < 60.0) {
        return;
    }

    if (!isCharging && !isSleeping) {
        // Discharging observation
        if (batteryPercent < s_lastPercent) {
            int deltaPercent = s_lastPercent - batteryPercent;
            double hours = deltaSec / 3600.0;
            double instantRate = (double)deltaPercent / hours;

            // Sanity check: valid keyboard discharge rate between 0.2% and 15% per hour
            if (instantRate >= 0.2 && instantRate <= 15.0) {
                if (!s_hasLearnedDischarge) {
                    s_dischargeRatePerHour = instantRate;
                    s_hasLearnedDischarge = TRUE;
                } else {
                    // Exponential moving average: 75% previous history, 25% new observation
                    s_dischargeRatePerHour = 0.75 * s_dischargeRatePerHour + 0.25 * instantRate;
                }
                BatteryStats_Save();
            }

            s_lastPercent = batteryPercent;
            s_lastTimestamp = now;
        } else if (batteryPercent > s_lastPercent) {
            // Percent increased while marked not charging (likely plugged in or battery rebound)
            s_lastPercent = batteryPercent;
            s_lastTimestamp = now;
        }
    } else if (isCharging) {
        // Charging observation
        if (batteryPercent > s_lastPercent) {
            int deltaPercent = batteryPercent - s_lastPercent;
            double hours = deltaSec / 3600.0;
            double instantRate = (double)deltaPercent / hours;

            // Sanity check: valid keyboard charge rate between 10% and 80% per hour
            if (instantRate >= 10.0 && instantRate <= 80.0) {
                if (!s_hasLearnedCharge) {
                    s_chargeRatePerHour = instantRate;
                    s_hasLearnedCharge = TRUE;
                } else {
                    s_chargeRatePerHour = 0.75 * s_chargeRatePerHour + 0.25 * instantRate;
                }
                BatteryStats_Save();
            }

            s_lastPercent = batteryPercent;
            s_lastTimestamp = now;
        } else if (batteryPercent < s_lastPercent) {
            s_lastPercent = batteryPercent;
            s_lastTimestamp = now;
        }
    }
}

BOOL BatteryStats_GetDischargeTimeRemaining(int currentPercent, int *outMinutes) {
    if (!outMinutes || currentPercent <= 0 || s_dischargeRatePerHour <= 0.05) {
        if (outMinutes) *outMinutes = 0;
        return FALSE;
    }

    double hours = (double)currentPercent / s_dischargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    if (mins < 1) mins = 1;
    *outMinutes = mins;
    return TRUE;
}

BOOL BatteryStats_GetChargeTimeTo85(int currentPercent, int *outMinutes) {
    if (!outMinutes) return FALSE;
    if (currentPercent >= 85) {
        *outMinutes = 0;
        return TRUE;
    }
    if (s_chargeRatePerHour <= 1.0) {
        *outMinutes = 0;
        return FALSE;
    }

    double delta = (double)(85 - currentPercent);
    double hours = delta / s_chargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    if (mins < 1) mins = 1;
    *outMinutes = mins;
    return TRUE;
}

BOOL BatteryStats_GetChargeTimeToFull(int currentPercent, int *outMinutes) {
    if (!outMinutes) return FALSE;
    if (currentPercent >= 100) {
        *outMinutes = 0;
        return TRUE;
    }
    if (s_chargeRatePerHour <= 1.0) {
        *outMinutes = 0;
        return FALSE;
    }

    double delta = (double)(100 - currentPercent);
    double hours = delta / s_chargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    if (mins < 1) mins = 1;
    *outMinutes = mins;
    return TRUE;
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
    if (s_dischargeRatePerHour <= 0.05) {
        *outHours = 0.0;
        return FALSE;
    }
    *outHours = 100.0 / s_dischargeRatePerHour;
    return TRUE;
}

BOOL BatteryStats_IsCalibrated(void) {
    return s_hasLearnedDischarge;
}
