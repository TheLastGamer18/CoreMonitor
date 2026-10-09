#include "icon_generator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// High-contrast 4x8 bitmap font for digits 0-9
// 4 bits wide per row (bits 3..0), 8 rows tall
static const unsigned char DIGIT_FONT_4X8[10][8] = {
    { 0x6, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x6 }, // 0: rounded oval
    { 0x4, 0xC, 0x4, 0x4, 0x4, 0x4, 0x4, 0xE }, // 1: top serif, straight spine, bottom base
    { 0x6, 0x9, 0x1, 0x2, 0x4, 0x8, 0x8, 0xF }, // 2: smooth top, diagonal stroke, base
    { 0xE, 0x1, 0x1, 0x7, 0x1, 0x1, 0x1, 0xE }, // 3: connected middle bar
    { 0x9, 0x9, 0x9, 0xF, 0x1, 0x1, 0x1, 0x1 }, // 4: open top, full bar, right leg
    { 0xF, 0x8, 0x8, 0xE, 0x1, 0x1, 0x1, 0xE }, // 5: top bar, left stroke, middle loop, bottom loop
    { 0x6, 0x8, 0x8, 0xE, 0x9, 0x9, 0x9, 0x6 }, // 6: smooth top, bottom loop
    { 0xF, 0x1, 0x1, 0x2, 0x2, 0x4, 0x4, 0x4 }, // 7: top bar, diagonal leg
    { 0x6, 0x9, 0x9, 0x6, 0x9, 0x9, 0x9, 0x6 }, // 8: symmetric double loop
    { 0x6, 0x9, 0x9, 0x7, 0x1, 0x1, 0x1, 0x6 }  // 9: top loop, right leg, bottom curl
};

// Compact 3x8 font for '0' in '100'
static const unsigned char DIGIT_FONT_3X8_0[8] = {
    0x2, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x2
};

// Compact 2x8 font for '1' in '100'
static const unsigned char DIGIT_FONT_2X8_1[8] = {
    0x2, 0x3, 0x1, 0x1, 0x1, 0x1, 0x1, 0x3
};

// 75% Mechanical Keyboard glyph (11x7, rows 4..10, cols 2..12)
// Features contoured chassis, top function row, alphanumeric row, and dedicated spacebar
static const char *KEYBOARD_ROWS[7] = {
    "01111111110", // Row 4: contoured top chassis
    "10101010101", // Row 5: function keys row
    "10000000001", // Row 6: chassis separator
    "10101010101", // Row 7: main alphanumeric keys
    "10000000001", // Row 8: chassis separator
    "10011111001", // Row 9: dedicated spacebar
    "01111111110"  // Row 10: contoured bottom chassis
};

// Energetic charging lightning bolt (5x7, rows 4..10, cols 5..9)
static const char *LIGHTNING_ROWS[7] = {
    "00110",
    "01100",
    "11110",
    "00111",
    "00110",
    "01100",
    "01000"
};

BOOL IconGen_IsTaskbarLight(void) {
    HKEY hKey;
    DWORD val = 0;
    DWORD size = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val != 0);
        }
        RegCloseKey(hKey);
    }
    return FALSE; // Windows default is dark taskbar
}

int IconGen_GetTargetIconSize(void) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef int (WINAPI *GetSystemMetricsForDpi_t)(int, UINT);
        typedef UINT (WINAPI *GetDpiForSystem_t)(void);
        GetSystemMetricsForDpi_t pfnMetrics = (GetSystemMetricsForDpi_t)GetProcAddress(hUser32, "GetSystemMetricsForDpi");
        GetDpiForSystem_t pfnDpi = (GetDpiForSystem_t)GetProcAddress(hUser32, "GetDpiForSystem");
        if (pfnMetrics && pfnDpi) {
            UINT dpi = pfnDpi();
            if (dpi > 0) {
                int cx = pfnMetrics(SM_CXSMICON, dpi);
                if (cx >= 16) return cx;
            }
        }
    }
    int cx = GetSystemMetrics(SM_CXSMICON);
    if (cx < 16) cx = 16;
    if (cx > 48) cx = 48;
    return cx;
}

static inline float ClampF(float v, float mn, float mx) {
    return (v < mn) ? mn : ((v > mx) ? mx : v);
}

// Draw a single pixel with alpha blending in premultiplied ARGB
static void DrawPixel(unsigned int *pixels, int size, int x, int y,
                      unsigned char r, unsigned char g, unsigned char b, float alpha) {
    if (x < 0 || x >= size || y < 0 || y >= size) return;
    float srcA = ClampF(alpha, 0.0f, 1.0f);
    if (srcA <= 0.0f) return;

    unsigned int bg = pixels[y * size + x];
    float dstA = (float)((bg >> 24) & 0xFF) / 255.0f;
    float dstR = (float)((bg >> 16) & 0xFF);
    float dstG = (float)((bg >> 8) & 0xFF);
    float dstB = (float)(bg & 0xFF);

    float outA = srcA + dstA * (1.0f - srcA);
    float outR = (float)r * srcA + dstR * (1.0f - srcA);
    float outG = (float)g * srcA + dstG * (1.0f - srcA);
    float outB = (float)b * srcA + dstB * (1.0f - srcA);

    unsigned char a = (unsigned char)ClampF(outA * 255.0f, 0.0f, 255.0f);
    unsigned char pr = (unsigned char)ClampF(outR, 0.0f, 255.0f);
    unsigned char pg = (unsigned char)ClampF(outG, 0.0f, 255.0f);
    unsigned char pb = (unsigned char)ClampF(outB, 0.0f, 255.0f);

    pixels[y * size + x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
}

// Maps a 16x16 grid coordinate to physical target pixel size
static void DrawGridCell(unsigned int *pixels, int size, int cellX, int cellY,
                         unsigned char r, unsigned char g, unsigned char b, float alpha) {
    if (size == 16) {
        DrawPixel(pixels, size, cellX, cellY, r, g, b, alpha);
        return;
    }
    int startX = (cellX * size) / 16;
    int endX   = ((cellX + 1) * size + 15) / 16;
    int startY = (cellY * size) / 16;
    int endY   = ((cellY + 1) * size + 15) / 16;
    if (endX > size) endX = size;
    if (endY > size) endY = size;
    for (int y = startY; y < endY; y++) {
        for (int x = startX; x < endX; x++) {
            DrawPixel(pixels, size, x, y, r, g, b, alpha);
        }
    }
}

HICON IconGen_CreateBatteryIcon(
    int batteryPercent,
    BOOL isCharging,
    float breathPhase,
    BOOL isSleeping,
    BOOL isDisconnected,
    CenterIconStyle centerStyle,
    BOOL lightTaskbar
) {
    int size = IconGen_GetTargetIconSize();

    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = size;
    bi.bV5Height = -size; // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(NULL);
    unsigned int *pixels = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (!hBitmap || !pixels) return NULL;
    memset(pixels, 0, size * size * sizeof(unsigned int));

    // Foreground colors: pure white on dark taskbar, deep charcoal on light taskbar
    unsigned char fgR = lightTaskbar ? 28 : 255;
    unsigned char fgG = lightTaskbar ? 28 : 255;
    unsigned char fgB = lightTaskbar ? 28 : 255;

    // Track ring color (dim background circle)
    unsigned char trackR = lightTaskbar ? 0 : 255;
    unsigned char trackG = lightTaskbar ? 0 : 255;
    unsigned char trackB = lightTaskbar ? 0 : 255;
    float trackAlpha = isDisconnected ? 0.15f : (isSleeping ? 0.20f : 0.25f);

    // Determine Active Arc Colors matching user reference image
    unsigned char arcR = 34, arcG = 197, arcB = 94; // Vibrant emerald green default
    if (isCharging) {
        // Bright emerald-cyan charging pulse
        arcR = 16; arcG = 185; arcB = 129;
    } else if (isSleeping || isDisconnected || batteryPercent < 0) {
        arcR = fgR; arcG = fgG; arcB = fgB;
    } else if (batteryPercent <= 20) {
        // Alert Red
        arcR = 239; arcG = 68; arcB = 68;
    } else if (batteryPercent <= 40) {
        // Warning Amber
        arcR = 245; arcG = 158; arcB = 11;
    } else if (batteryPercent > 85) {
        // High battery: crisp bright mint/emerald
        arcR = 52; arcG = 211; arcB = 153;
    } else {
        // Standard healthy green
        arcR = 34; arcG = 197; arcB = 94;
    }

    // Breathing pulse calculation when charging
    float breathMultiplier = 1.0f;
    if (isCharging) {
        float k = powf(0.5f + 0.5f * cosf(breathPhase), 0.75f);
        breathMultiplier = 0.35f + 0.65f * k;
    }

    // Normalized geometry setup:
    // Center at (0.0, 0.0), extent [-1.0, 1.0]
    float halfSize = (float)size * 0.5f;
    float pxStep = 1.0f / halfSize;

    // Ring proportions:
    // Radius = 0.88, Thickness = 0.17
    // Spans the full icon bounding box (16px) with maximal interior diameter (~12.8px)
    const float ringRadius = 0.88f;
    const float ringThickness = 0.17f;
    const float halfThickness = ringThickness * 0.5f;

    int effPercent = (batteryPercent < 0) ? 0 : (batteryPercent > 100 ? 100 : batteryPercent);
    float maxAngle = ((float)effPercent / 100.0f) * 2.0f * PI;

    // Center coordinates for rounded endcaps
    float capStartX = 0.0f;
    float capStartY = -ringRadius;
    float capEndX = ringRadius * sinf(maxAngle);
    float capEndY = -ringRadius * cosf(maxAngle);

    // 1. RENDER CIRCLE INDICATOR RING
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float u = ((float)x + 0.5f - halfSize) / halfSize;
            float v = ((float)y + 0.5f - halfSize) / halfSize;
            float dist = sqrtf(u * u + v * v);

            float dRing = fabsf(dist - ringRadius);
            float ringDist = dRing - halfThickness;
            if (ringDist > pxStep) continue;

            float ringCoverage = ClampF(0.5f - ringDist / pxStep, 0.0f, 1.0f);
            if (ringCoverage <= 0.0f) continue;

            float angle = atan2f(u, -v);
            if (angle < 0.0f) angle += 2.0f * PI;

            BOOL inArc = FALSE;
            float arcCoverage = 0.0f;

            if (!isDisconnected && effPercent > 0) {
                if (effPercent >= 100) {
                    inArc = TRUE;
                    arcCoverage = ringCoverage;
                } else if (angle <= maxAngle) {
                    inArc = TRUE;
                    arcCoverage = ringCoverage;
                } else {
                    float dStart = sqrtf((u - capStartX)*(u - capStartX) + (v - capStartY)*(v - capStartY));
                    float dEnd   = sqrtf((u - capEndX)*(u - capEndX) + (v - capEndY)*(v - capEndY));
                    float minCap = (dStart < dEnd) ? dStart : dEnd;
                    float capDist = minCap - halfThickness;
                    if (capDist <= pxStep) {
                        inArc = TRUE;
                        arcCoverage = ClampF(0.5f - capDist / pxStep, 0.0f, 1.0f);
                    }
                }
            }

            float pA, pR, pG, pB;
            if (inArc) {
                pR = (float)arcR; pG = (float)arcG; pB = (float)arcB;
                float arcAlpha = isSleeping ? 0.38f : (isCharging ? breathMultiplier : 1.0f);
                pA = arcCoverage * 255.0f * arcAlpha;
            } else {
                pR = (float)trackR; pG = (float)trackG; pB = (float)trackB;
                pA = ringCoverage * 255.0f * trackAlpha;
            }

            unsigned char a = (unsigned char)ClampF(pA, 0.0f, 255.0f);
            unsigned char r = (unsigned char)(pR * (a / 255.0f));
            unsigned char g = (unsigned char)(pG * (a / 255.0f));
            unsigned char b = (unsigned char)(pB * (a / 255.0f));

            pixels[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // 2. RENDER CENTER GLYPH
    float centerAlpha = isDisconnected ? 0.35f : (isSleeping ? 0.45f : 1.0f);

    if (centerStyle == ICON_CENTER_PERCENT && effPercent >= 0 && !isDisconnected && !isSleeping) {
        // Large, bold, high-contrast numeric percentage
        if (effPercent < 10) {
            // Single digit: centered at col 6..9, row 4..11
            for (int row = 0; row < 8; row++) {
                unsigned char bits = DIGIT_FONT_4X8[effPercent][row];
                for (int col = 0; col < 4; col++) {
                    if (bits & (1 << (3 - col))) {
                        DrawGridCell(pixels, size, 6 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                }
            }
        } else if (effPercent < 100) {
            // Two digits: d1 at cols 3..6, d2 at cols 8..11, row 4..11
            int d1 = effPercent / 10;
            int d2 = effPercent % 10;
            for (int row = 0; row < 8; row++) {
                unsigned char bits1 = DIGIT_FONT_4X8[d1][row];
                unsigned char bits2 = DIGIT_FONT_4X8[d2][row];
                for (int col = 0; col < 4; col++) {
                    if (bits1 & (1 << (3 - col))) {
                        DrawGridCell(pixels, size, 3 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                    if (bits2 & (1 << (3 - col))) {
                        DrawGridCell(pixels, size, 8 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                }
            }
        } else {
            // 100%: '1' at cols 3..4, '0' at cols 6..8, '0' at cols 10..12
            for (int row = 0; row < 8; row++) {
                unsigned char b1 = DIGIT_FONT_2X8_1[row];
                if (b1 & 2) DrawGridCell(pixels, size, 3, 4 + row, fgR, fgG, fgB, centerAlpha);
                if (b1 & 1) DrawGridCell(pixels, size, 4, 4 + row, fgR, fgG, fgB, centerAlpha);

                unsigned char b0 = DIGIT_FONT_3X8_0[row];
                for (int col = 0; col < 3; col++) {
                    if (b0 & (1 << (2 - col))) {
                        DrawGridCell(pixels, size, 6 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                        DrawGridCell(pixels, size, 10 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                }
            }
        }
    } else {
        // Detailed 75% Mechanical Keyboard Glyph (11x7, rows 4..10, cols 2..12)
        for (int r = 0; r < 7; r++) {
            const char *line = KEYBOARD_ROWS[r];
            for (int c = 0; c < 11; c++) {
                if (line[c] == '1') {
                    DrawGridCell(pixels, size, 2 + c, 4 + r, fgR, fgG, fgB, centerAlpha);
                }
            }
        }

        // Active charging overlay: vivid electric gold lightning bolt
        if (isCharging) {
            float boltAlpha = (0.75f + 0.25f * breathMultiplier);
            for (int r = 0; r < 7; r++) {
                const char *bline = LIGHTNING_ROWS[r];
                for (int c = 0; c < 5; c++) {
                    if (bline[c] == '1') {
                        DrawGridCell(pixels, size, 5 + c, 4 + r, 251, 191, 36, boltAlpha);
                    }
                }
            }
        }
    }

    HBITMAP hMono = CreateBitmap(size, size, 1, 1, NULL);
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hMono;
    ii.hbmColor = hBitmap;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBitmap);
    DeleteObject(hMono);
    return hIcon;
}
