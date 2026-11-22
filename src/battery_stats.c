#include "battery_stats.h"
#include <stdio.h>

#define CURRENT_STATS_VERSION 2

static void LoadFromKey(BatteryStatsContext *ctx, const WCHAR *keyPath) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVersion = 0;
        DWORD dwSize = sizeof(DWORD);

        if (RegQueryValueExW(hKey, L"StatsVersion", NULL, NULL, (LPBYTE)&dwVersion, &dwSize) == ERROR_SUCCESS && dwVersion >= CURRENT_STATS_VERSION) {
            DWORD dwDischargeMilli = 0;
            DWORD dwChargeMilli = 0;
            DWORD dwAccumSec = 0;
            DWORD dwBasePercent = 0;

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"DischargeMilliRate", NULL, NULL, (LPBYTE)&dwDischargeMilli, &dwSize) == ERROR_SUCCESS) {
                if (dwDischargeMilli >= 10 && dwDischargeMilli <= 50000) { // 0.01% to 50% per hr
                    ctx->dischargeRatePerHour = (double)dwDischargeMilli / 1000.0;
                    ctx->hasLearnedDischarge = TRUE;
                }
            }

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"ChargeMilliRate", NULL, NULL, (LPBYTE)&dwChargeMilli, &dwSize) == ERROR_SUCCESS) {
                if (dwChargeMilli >= 5000 && dwChargeMilli <= 100000) { // 5% to 100% per hr
                    ctx->chargeRatePerHour = (double)dwChargeMilli / 1000.0;
                    ctx->hasLearnedCharge = TRUE;
                }
            }

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"AccumDischargeSec", NULL, NULL, (LPBYTE)&dwAccumSec, &dwSize) == ERROR_SUCCESS) {
                if (dwAccumSec <= 86400 * 30) {
                    ctx->accumDischargeSec = dwAccumSec;
                }
            }

            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"BaselinePercent", NULL, NULL, (LPBYTE)&dwBasePercent, &dwSize) == ERROR_SUCCESS) {
                if (dwBasePercent <= 100) {
                    ctx->baselinePercent = (int)dwBasePercent;
                }
            }
        }
        RegCloseKey(hKey);
    }
}

void BatteryStats_Init(BatteryStatsContext *ctx, const WCHAR *regSubKey, const WCHAR *legacyRegKey, double defDischarge, double defCharge) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(BatteryStatsContext));
    ctx->dischargeRatePerHour = defDischarge;
    ctx->chargeRatePerHour = defCharge;
    ctx->baselinePercent = -1;
    ctx->regSubKey = regSubKey;
    ctx->legacyRegSubKey = legacyRegKey;

    // Try primary key first
    if (ctx->regSubKey && ctx->regSubKey[0] != L'\0') {
        LoadFromKey(ctx, ctx->regSubKey);
    }

    // If primary had no learned rates, check fallback legacy key
    if (!ctx->hasLearnedDischarge && ctx->legacyRegSubKey && ctx->legacyRegSubKey[0] != L'\0') {
        LoadFromKey(ctx, ctx->legacyRegSubKey);
    }
}

static void SaveToKey(const BatteryStatsContext *ctx, const WCHAR *keyPath) {
    if (!ctx || !keyPath || keyPath[0] == L'\0') return;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD dwVersion = CURRENT_STATS_VERSION;
        DWORD dwDischargeMilli = (DWORD)(ctx->dischargeRatePerHour * 1000.0 + 0.5);
        DWORD dwChargeMilli = (DWORD)(ctx->chargeRatePerHour * 1000.0 + 0.5);
        DWORD dwAccumSec = ctx->accumDischargeSec;
        DWORD dwBasePercent = (ctx->baselinePercent >= 0) ? (DWORD)ctx->baselinePercent : 0xFFFFFFFF;

        RegSetValueExW(hKey, L"StatsVersion", 0, REG_DWORD, (const BYTE*)&dwVersion, sizeof(DWORD));
        RegSetValueExW(hKey, L"DischargeMilliRate", 0, REG_DWORD, (const BYTE*)&dwDischargeMilli, sizeof(DWORD));
        RegSetValueExW(hKey, L"ChargeMilliRate", 0, REG_DWORD, (const BYTE*)&dwChargeMilli, sizeof(DWORD));
        RegSetValueExW(hKey, L"AccumDischargeSec", 0, REG_DWORD, (const BYTE*)&dwAccumSec, sizeof(DWORD));
        RegSetValueExW(hKey, L"BaselinePercent", 0, REG_DWORD, (const BYTE*)&dwBasePercent, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

void BatteryStats_Save(BatteryStatsContext *ctx) {
    if (!ctx) return;
    if (ctx->regSubKey && ctx->regSubKey[0] != L'\0') {
        SaveToKey(ctx, ctx->regSubKey);
    }
}

void BatteryStats_ResetSessionBaseline(BatteryStatsContext *ctx) {
    if (ctx) {
        ctx->lastMonitoredTick = 0;
    }
}

void BatteryStats_Record(BatteryStatsContext *ctx, int batteryPercent, BOOL isCharging, BOOL isDisconnected) {
    if (!ctx) return;
    if (isDisconnected || batteryPercent < 0 || batteryPercent > 100) {
        ctx->lastMonitoredTick = 0;
        return;
    }

    DWORD nowTick = GetTickCount();

    if (ctx->lastMonitoredTick == 0 || ctx->baselinePercent < 0) {
        ctx->baselinePercent = batteryPercent;
        ctx->accumDischargeSec = 0;
        ctx->accumChargeSec = 0;
        ctx->lastWasCharging = isCharging;
        ctx->lastMonitoredTick = nowTick;
        return;
    }

    DWORD deltaMs = nowTick - ctx->lastMonitoredTick;
    ctx->lastMonitoredTick = nowTick;
    DWORD deltaSec = deltaMs / 1000;

    BOOL isContinuousMonitored = (deltaSec > 0 && deltaSec <= 120);

    if (ctx->lastWasCharging != isCharging) {
        ctx->baselinePercent = batteryPercent;
        ctx->lastWasCharging = isCharging;
        ctx->accumDischargeSec = 0;
        ctx->accumChargeSec = 0;
        return;
    }

    if (!isCharging) {
        // DISCHARGING
        if (isContinuousMonitored) {
            ctx->accumDischargeSec += deltaSec;
        }

        if (batteryPercent < ctx->baselinePercent) {
            int deltaPercent = ctx->baselinePercent - batteryPercent;
            double hours = (double)ctx->accumDischargeSec / 3600.0;

            BOOL shouldUpdate = (deltaPercent >= 2) || (deltaPercent >= 1 && hours >= 1.0);

            if (shouldUpdate && hours > 0.05) {
                double instantRate = (double)deltaPercent / hours;

                if (instantRate >= 0.01 && instantRate <= 30.0) {
                    if (!ctx->hasLearnedDischarge) {
                        ctx->dischargeRatePerHour = instantRate;
                        ctx->hasLearnedDischarge = TRUE;
                    } else {
                        ctx->dischargeRatePerHour = 0.80 * ctx->dischargeRatePerHour + 0.20 * instantRate;
                    }
                    BatteryStats_Save(ctx);
                }

                ctx->baselinePercent = batteryPercent;
                ctx->accumDischargeSec = 0;
            }
        } else if (batteryPercent > ctx->baselinePercent) {
            ctx->baselinePercent = batteryPercent;
        }
    } else {
        // CHARGING
        if (isContinuousMonitored) {
            ctx->accumChargeSec += deltaSec;
        }

        if (batteryPercent > ctx->baselinePercent) {
            int deltaPercent = batteryPercent - ctx->baselinePercent;
            double hours = (double)ctx->accumChargeSec / 3600.0;

            BOOL shouldUpdate = (deltaPercent >= 2) || (deltaPercent >= 1 && hours >= 0.25);

            if (shouldUpdate && hours > 0.05) {
                double instantRate = (double)deltaPercent / hours;

                if (instantRate >= 5.0 && instantRate <= 100.0) {
                    if (!ctx->hasLearnedCharge) {
                        ctx->chargeRatePerHour = instantRate;
                        ctx->hasLearnedCharge = TRUE;
                    } else {
                        ctx->chargeRatePerHour = 0.75 * ctx->chargeRatePerHour + 0.25 * instantRate;
                    }
                    BatteryStats_Save(ctx);
                }

                ctx->baselinePercent = batteryPercent;
                ctx->accumChargeSec = 0;
            }
        } else if (batteryPercent < ctx->baselinePercent) {
            ctx->baselinePercent = batteryPercent;
        }
    }
}

static BOOL BatteryStats_GetDischargeTimeRemaining(const BatteryStatsContext *ctx, int currentPercent, int *outMinutes) {
    if (!ctx || !outMinutes || currentPercent <= 0 || ctx->dischargeRatePerHour <= 0.005) {
        if (outMinutes) *outMinutes = 0;
        return FALSE;
    }

    double hours = (double)currentPercent / ctx->dischargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    if (mins < 1) mins = 1;
    *outMinutes = mins;
    return TRUE;
}

static BOOL CalcTimeToPercent(const BatteryStatsContext *ctx, int currentPercent, int targetPercent, int *outMinutes) {
    if (!ctx || !outMinutes) return FALSE;
    if (currentPercent >= targetPercent) {
        *outMinutes = 0;
        return TRUE;
    }
    if (ctx->chargeRatePerHour <= 1.0) {
        *outMinutes = 0;
        return FALSE;
    }
    double hours = (double)(targetPercent - currentPercent) / ctx->chargeRatePerHour;
    int mins = (int)(hours * 60.0 + 0.5);
    *outMinutes = (mins < 1) ? 1 : mins;
    return TRUE;
}

static BOOL BatteryStats_GetChargeTimeTo85(const BatteryStatsContext *ctx, int currentPercent, int *outMinutes) {
    return CalcTimeToPercent(ctx, currentPercent, 85, outMinutes);
}

static BOOL BatteryStats_GetChargeTimeToFull(const BatteryStatsContext *ctx, int currentPercent, int *outMinutes) {
    return CalcTimeToPercent(ctx, currentPercent, 100, outMinutes);
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
    const BatteryStatsContext *ctx,
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
            if (BatteryStats_GetChargeTimeToFull(ctx, currentPercent, &minsFull)) {
                WCHAR timeBuf[32];
                FormatMinutesToText(minsFull, timeBuf, 32);
                _snwprintf(outBuffer, bufferCharCount, L"%s to full", timeBuf);
            } else {
                wcsncpy(outBuffer, L"Charging...", bufferCharCount - 1);
            }
        } else {
            int mins85 = 0;
            if (BatteryStats_GetChargeTimeTo85(ctx, currentPercent, &mins85)) {
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
    if (BatteryStats_GetDischargeTimeRemaining(ctx, currentPercent, &minsRem)) {
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

double BatteryStats_GetDischargeRatePerHour(const BatteryStatsContext *ctx) {
    return ctx ? ctx->dischargeRatePerHour : 0.0;
}

double BatteryStats_GetChargeRatePerHour(const BatteryStatsContext *ctx) {
    return ctx ? ctx->chargeRatePerHour : 0.0;
}

BOOL BatteryStats_GetEstimatedEnduranceHours(const BatteryStatsContext *ctx, double *outHours) {
    if (!ctx || !outHours) return FALSE;
    if (ctx->dischargeRatePerHour <= 0.005) {
        *outHours = 0.0;
        return FALSE;
    }
    *outHours = 100.0 / ctx->dischargeRatePerHour;
    return TRUE;
}

BOOL BatteryStats_IsCalibrated(const BatteryStatsContext *ctx) {
    return ctx ? ctx->hasLearnedDischarge : FALSE;
}
