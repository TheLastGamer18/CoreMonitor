#ifndef ICON_GENERATOR_H
#define ICON_GENERATOR_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ICON_CENTER_KEYBOARD = 0,
    ICON_CENTER_PERCENT
} CenterIconStyle;

// Checks whether Windows taskbar currently uses light theme
BOOL IconGen_IsTaskbarLight(void);

// Returns the optimal native small icon size for the current display DPI
int IconGen_GetTargetIconSize(void);

// Generates a 32-bit ARGB HICON with antialiased circle indicator ring,
// smooth rounded endcaps, optional breathing pulse, and stylized 75% keyboard glyph.
HICON IconGen_CreateBatteryIcon(
    int batteryPercent,          // 0 - 100, or -1 if offline/disconnected
    BOOL isCharging,             // TRUE to enable green charging arc + breathing
    float breathPhase,           // 0.0 to 2*PI for smooth breathing animation
    BOOL isSleeping,             // TRUE if dongle present but keyboard asleep
    BOOL isDisconnected,         // TRUE if completely disconnected
    CenterIconStyle centerStyle, // KEYBOARD or PERCENT
    BOOL lightTaskbar            // TRUE for light taskbar, FALSE for dark
);

#ifdef __cplusplus
}
#endif

#endif // ICON_GENERATOR_H
