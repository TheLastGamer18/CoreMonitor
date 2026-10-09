#ifndef BATTERY_STATS_H
#define BATTERY_STATS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize statistics engine and load persisted historical data from registry
void BatteryStats_Init(void);

// Record a new battery polling observation
void BatteryStats_Record(int batteryPercent, BOOL isCharging, BOOL isSleeping, BOOL isDisconnected);

// Estimates minutes remaining on battery based on historical discharge rate
// Returns TRUE if valid estimate available, FALSE otherwise
BOOL BatteryStats_GetDischargeTimeRemaining(int currentPercent, int *outMinutes);

// Estimates minutes until 85% battery level (for lithium longevity)
BOOL BatteryStats_GetChargeTimeTo85(int currentPercent, int *outMinutes);

// Estimates minutes until 100% battery level
BOOL BatteryStats_GetChargeTimeToFull(int currentPercent, int *outMinutes);

// Formats a user-friendly string describing the battery estimate (e.g., "~18h 30m remaining", "~45m to 85%")
void BatteryStats_FormatRemainingText(
    int currentPercent,
    BOOL isCharging,
    BOOL isSleeping,
    BOOL isDisconnected,
    WCHAR *outBuffer,
    DWORD bufferCharCount
);

// Returns estimated discharge rate in percent per hour (e.g. 1.5% / hour)
double BatteryStats_GetDischargeRatePerHour(void);

// Returns estimated charge rate in percent per hour (e.g. 35.0% / hour)
double BatteryStats_GetChargeRatePerHour(void);

// Returns estimated full-charge endurance in hours based on discharge rate (e.g. 50.0 hours)
BOOL BatteryStats_GetEstimatedEnduranceHours(double *outHours);

// Persists learned statistics to registry
void BatteryStats_Save(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_STATS_H
