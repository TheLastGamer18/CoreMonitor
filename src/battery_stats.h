#ifndef BATTERY_STATS_H
#define BATTERY_STATS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double dischargeRatePerHour;
    double chargeRatePerHour;
    BOOL hasLearnedDischarge;
    BOOL hasLearnedCharge;
    DWORD lastMonitoredTick;
    int baselinePercent;
    BOOL lastWasCharging;
    DWORD accumDischargeSec;
    DWORD accumChargeSec;
    WCHAR regSubKey[128];
    WCHAR legacyRegSubKey[128]; // Optional fallback key for seamless upgrade
} BatteryStatsContext;

// Initialize statistics engine instance and load persisted historical data from registry
void BatteryStats_Init(BatteryStatsContext *ctx, const WCHAR *regSubKey, const WCHAR *legacyRegKey, double defDischarge, double defCharge);

// Resets elapsed time calculation upon PC resume/wake so sleep duration doesn't dilute discharge rate
void BatteryStats_ResetSessionBaseline(BatteryStatsContext *ctx);

// Record a new battery polling observation
void BatteryStats_Record(BatteryStatsContext *ctx, int batteryPercent, BOOL isCharging, BOOL isDisconnected);

// Estimates minutes remaining on battery based on historical discharge rate
BOOL BatteryStats_GetDischargeTimeRemaining(const BatteryStatsContext *ctx, int currentPercent, int *outMinutes);

// Estimates minutes until 85% battery level (for lithium longevity)
BOOL BatteryStats_GetChargeTimeTo85(const BatteryStatsContext *ctx, int currentPercent, int *outMinutes);

// Estimates minutes until 100% battery level
BOOL BatteryStats_GetChargeTimeToFull(const BatteryStatsContext *ctx, int currentPercent, int *outMinutes);

// Formats a user-friendly string describing the battery estimate (e.g., "~18h 30m remaining", "~45m to 85%")
void BatteryStats_FormatRemainingText(
    const BatteryStatsContext *ctx,
    int currentPercent,
    BOOL isCharging,
    BOOL isSleeping,
    BOOL isDisconnected,
    WCHAR *outBuffer,
    DWORD bufferCharCount
);

// Returns estimated discharge rate in percent per hour
double BatteryStats_GetDischargeRatePerHour(const BatteryStatsContext *ctx);

// Returns estimated charge rate in percent per hour
double BatteryStats_GetChargeRatePerHour(const BatteryStatsContext *ctx);

// Returns estimated full-charge endurance in hours based on discharge rate
BOOL BatteryStats_GetEstimatedEnduranceHours(const BatteryStatsContext *ctx, double *outHours);

// Returns TRUE if discharge statistics have been calibrated via historical data or runtime learning
BOOL BatteryStats_IsCalibrated(const BatteryStatsContext *ctx);

// Persists learned statistics to registry
void BatteryStats_Save(BatteryStatsContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_STATS_H
