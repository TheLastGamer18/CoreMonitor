#ifndef ICON_GENERATOR_H
#define ICON_GENERATOR_H

#include <windows.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ICON_CENTER_GLYPH = 0, // Shows model glyph (Core75 keyboard or CoreM1 mouse)
    ICON_CENTER_PERCENT,   // Shows 2-3 digit battery percentage number
    ICON_CENTER_RING_ONLY  // Thickened circular battery ring with empty center
} CenterIconStyle;

// Checks whether Windows taskbar currently uses light theme
BOOL IconGen_IsTaskbarLight(void);

// Returns the optimal native small icon size for the current display DPI
int IconGen_GetTargetIconSize(void);

// Generates a 32-bit ARGB HICON with antialiased circle indicator ring,
// smooth rounded endcaps, optional breathing pulse, and model-specific glyph.
HICON IconGen_CreateBatteryIcon(
    int batteryPercent,          // 0 - 100, or -1 if offline/disconnected
    BOOL isCharging,             // TRUE to enable green charging arc + breathing
    float breathPhase,           // 0.0 to 2*PI for smooth breathing animation
    BOOL isSleeping,             // TRUE if dongle present but peripheral asleep
    BOOL isDisconnected,         // TRUE if completely disconnected
    CenterIconStyle centerStyle, // GLYPH, PERCENT, or RING_ONLY
    DeviceModel model,           // MODEL_CORE75 or MODEL_COREM1
    BOOL lightTaskbar            // TRUE for light taskbar, FALSE for dark
);

#ifdef __cplusplus
}
#endif

#endif // ICON_GENERATOR_H
