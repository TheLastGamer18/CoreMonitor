#ifndef SETTINGS_H
#define SETTINGS_H

#include <windows.h>
#include "icon_generator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    CenterIconStyle centerStyle;
    BOOL startWithWindows;
    BOOL lowBatteryAlert;              // Low battery alert enabled
    int lowAlertThreshold;             // 10, 15, or 20 (default 20)
    BOOL longevityAlert;               // Longevity battery alert enabled
    int longevityAlertThreshold;       // 80 or 85 (default 85)
    BOOL highBatteryAlert;             // Full battery alert enabled
    int highAlertThreshold;            // 95 or 100 (default 100)
    BOOL lowAlertShownForDischarge;    // Prevents repeating low battery notifications
    BOOL longevityAlertShownForCharge; // Prevents repeating longevity battery notifications
    BOOL highAlertShownForCharge;      // Prevents repeating high battery notifications
} AppSettings;

extern AppSettings g_Settings;

void Settings_Load(void);
void Settings_Save(void);
void Settings_SetAutostart(BOOL enable);
BOOL Settings_GetAutostart(void);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
