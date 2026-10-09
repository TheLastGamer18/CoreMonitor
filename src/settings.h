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
    BOOL lowBatteryAlert;           // Notify when battery <= 20%
    BOOL highBatteryAlert;          // Notify when battery >= 85% (protects battery health)
    BOOL lowAlertShownForDischarge; // Prevents repeating low battery notifications
    BOOL highAlertShownForCharge;   // Prevents repeating high battery notifications
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
