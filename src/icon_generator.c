#include "icon_generator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// High-contrast, maximum-legibility 3x7 bitmap font for digits 0-9
// 3 bits wide per row (bits 2..0), 7 rows tall (rows 4..10 in 16x16 grid)
static const unsigned char DIGIT_FONT_3X7[10][7] = {
    { 0x7, 0x5, 0x5, 0x5, 0x5, 0x5, 0x7 }, // 0: tall open oval counter
    { 0x2, 0x6, 0x2, 0x2, 0x2, 0x2, 0x7 }, // 1: classic balanced serif 1
    { 0x7, 0x1, 0x1, 0x7, 0x4, 0x4, 0x7 }, // 2: symmetric open loops
    { 0x7, 0x1, 0x1, 0x7, 0x1, 0x1, 0x7 }, // 3: distinct open curves
    { 0x5, 0x5, 0x5, 0x7, 0x1, 0x1, 0x1 }, // 4: bold open counter
    { 0x7, 0x4, 0x4, 0x7, 0x1, 0x1, 0x7 }, // 5: symmetric open loops
    { 0x7, 0x4, 0x4, 0x7, 0x5, 0x5, 0x7 }, // 6: clear open top stem, distinct bowl
    { 0x7, 0x1, 0x1, 0x2, 0x2, 0x4, 0x4 }, // 7: smooth 2-row diagonal descent
    { 0x7, 0x5, 0x5, 0x7, 0x5, 0x5, 0x7 }, // 8: dual distinct 2-row open counters
    { 0x7, 0x5, 0x5, 0x7, 0x1, 0x1, 0x7 }  // 9: distinct top bowl, clean bottom closure
};

// Contour-proportioned 75% Mechanical Keyboard glyph (10x7, rows 4..10, cols 3..12)
// Bold iconic silhouette filling the expanded inner circle
static const char *KEYBOARD_ROWS[7] = {
    "0111111110", // Row 4: rounded top chassis
    "1101101101", // Row 5: top cluster / function keys
    "1010101011", // Row 6: upper alphanumeric row
    "1101010101", // Row 7: lower alphanumeric row
    "1000000001", // Row 8: chassis separator
    "1011111101", // Row 9: dedicated wide spacebar cluster
    "0111111110"  // Row 10: rounded bottom chassis
};

// Energetic charging lightning bolt overlay (5x7, rows 4..10, cols 5..9)
static const char *LIGHTNING_ROWS[7] = {
    "00110", // Row 4
    "01100", // Row 5
    "11110", // Row 6
    "00111", // Row 7
    "00110", // Row 8
    "01100", // Row 9
    "01000"  // Row 10
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

    // Track ring color (clean, distinct baseline circle)
    unsigned char trackR = lightTaskbar ? 0 : 255;
    unsigned char trackG = lightTaskbar ? 0 : 255;
    unsigned char trackB = lightTaskbar ? 0 : 255;
    float trackAlpha = isDisconnected ? 0.25f : (isSleeping ? 0.35f : 0.55f);

    // Active Arc Colors: high-luminance, vibrant palette tailored for taskbars
    unsigned char arcR = 46, arcG = 213, arcB = 115; // Crisp emerald default (lum ~175)
    if (isCharging) {
        // Vivid cyan-emerald pulse (lum ~185)
        arcR = 20; arcG = 220; arcB = 160;
    } else if (isSleeping || isDisconnected || batteryPercent < 0) {
        arcR = fgR; arcG = fgG; arcB = fgB;
    } else if (batteryPercent <= 20) {
        // High-contrast alert coral red
        arcR = 248; arcG = 81; arcB = 73;
    } else if (batteryPercent <= 40) {
        // Electric warning amber
        arcR = 251; arcG = 191; arcB = 36;
    } else if (batteryPercent > 85) {
        // Crisp high-battery mint emerald
        arcR = 52; arcG = 211; arcB = 153;
    } else {
        // Healthy green
        arcR = 46; arcG = 213; arcB = 115;
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
    // Radius = 0.885, Thickness = 0.23
    // Expands outer boundary to full 16x16 canvas edge, maximizing perceived size
    // and widening the inner clearance zone to ~12.3px for larger, bolder center content
    const float ringRadius = 0.885f;
    const float ringThickness = 0.23f;
    const float halfThickness = ringThickness * 0.5f;

    int effPercent = (batteryPercent < 0) ? 0 : (batteryPercent > 100 ? 100 : batteryPercent);
    float maxAngle = ((float)effPercent / 100.0f) * 2.0f * PI;

    // Center coordinates for rounded endcaps
    float capStartX = 0.0f;
    float capStartY = -ringRadius;
    float capEndX = ringRadius * sinf(maxAngle);
    float capEndY = -ringRadius * cosf(maxAngle);

    // 1. RENDER CIRCLE INDICATOR RING (Seamless Active Arc Overlaid on Baseline Track)
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

            // Layer active arc cleanly over the full continuous baseline track
            float tAlpha = ringCoverage * trackAlpha;
            float aAlpha = 0.0f;
            if (inArc) {
                float arcAlpha = isSleeping ? 0.38f : (isCharging ? breathMultiplier : 1.0f);
                aAlpha = arcCoverage * arcAlpha;
            }

            float outA = aAlpha + tAlpha * (1.0f - aAlpha);
            if (outA <= 0.0f) continue;

            float pR = (float)arcR * aAlpha + (float)trackR * tAlpha * (1.0f - aAlpha);
            float pG = (float)arcG * aAlpha + (float)trackG * tAlpha * (1.0f - aAlpha);
            float pB = (float)arcB * aAlpha + (float)trackB * tAlpha * (1.0f - aAlpha);

            unsigned char a = (unsigned char)ClampF(outA * 255.0f, 0.0f, 255.0f);
            unsigned char r = (unsigned char)ClampF(pR, 0.0f, 255.0f);
            unsigned char g = (unsigned char)ClampF(pG, 0.0f, 255.0f);
            unsigned char b = (unsigned char)ClampF(pB, 0.0f, 255.0f);

            pixels[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // 2. RENDER CENTER GLYPH
    float centerAlpha = isDisconnected ? 0.35f : (isSleeping ? 0.45f : 1.0f);

    if (centerStyle == ICON_CENTER_PERCENT && effPercent >= 0 && !isDisconnected) {
        // High-contrast, maximum-legibility numeric percentage (rows 4..10, 7 rows tall)
        if (effPercent < 10) {
            // Single digit: centered at cols 6..8, rows 4..10
            for (int row = 0; row < 7; row++) {
                unsigned char bits = DIGIT_FONT_3X7[effPercent][row];
                for (int col = 0; col < 3; col++) {
                    if (bits & (1 << (2 - col))) {
                        DrawGridCell(pixels, size, 6 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                }
            }
        } else if (effPercent < 100) {
            // Two digits: d1 at cols 4..6, d2 at cols 9..11 (2px gap, perfectly centered span 4..11), rows 4..10
            int d1 = effPercent / 10;
            int d2 = effPercent % 10;
            for (int row = 0; row < 7; row++) {
                unsigned char bits1 = DIGIT_FONT_3X7[d1][row];
                unsigned char bits2 = DIGIT_FONT_3X7[d2][row];
                for (int col = 0; col < 3; col++) {
                    if (bits1 & (1 << (2 - col))) {
                        DrawGridCell(pixels, size, 4 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                    if (bits2 & (1 << (2 - col))) {
                        DrawGridCell(pixels, size, 9 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                }
            }
        } else {
            // 100%: '1' spine at col 4 (serif at col 3), '0' at cols 6..8, '0' at cols 10..12, rows 4..10 (span 3..12)
            for (int row = 0; row < 7; row++) {
                DrawGridCell(pixels, size, 4, 4 + row, fgR, fgG, fgB, centerAlpha);
                if (row == 0 || row == 1 || row == 6) {
                    DrawGridCell(pixels, size, 3, 4 + row, fgR, fgG, fgB, centerAlpha);
                }

                unsigned char b0 = DIGIT_FONT_3X7[0][row];
                for (int col = 0; col < 3; col++) {
                    if (b0 & (1 << (2 - col))) {
                        DrawGridCell(pixels, size, 6 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                        DrawGridCell(pixels, size, 10 + col, 4 + row, fgR, fgG, fgB, centerAlpha);
                    }
                }
            }
        }
    } else {
        // Contour-proportioned 75% Mechanical Keyboard Glyph (10x7, rows 4..10, cols 3..12)
        for (int r = 0; r < 7; r++) {
            const char *line = KEYBOARD_ROWS[r];
            for (int c = 0; c < 10; c++) {
                if (line[c] == '1') {
                    DrawGridCell(pixels, size, 3 + c, 4 + r, fgR, fgG, fgB, centerAlpha);
                }
            }
        }

        // Active charging overlay: vivid electric gold lightning bolt
        if (isCharging) {
            float boltAlpha = (0.85f + 0.15f * breathMultiplier);
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
