#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JPAssistNativeHighlight {
    uint32_t start;
    uint32_t length;
} JPAssistNativeHighlight;

typedef struct JPAssistNativeTextboxBounds {
    int16_t y;
    int16_t height;
    int16_t logicalScreenHeight;
} JPAssistNativeTextboxBounds;

// Controller icons occupy one native textbox glyph but are expanded to
// readable ASCII markers in the normalized corpus. Return that marker length
// so native rendering can remain in the same coordinate space as token spans.
static inline uint32_t JPAssist_GetNormalizedGlyphLength(uint16_t character) {
    switch (character) {
        case 0x839F: // [A]
        case 0x83A0: // [B]
        case 0x83A1: // [C]
        case 0x83A2: // [L]
        case 0x83A3: // [R]
        case 0x83A4: // [Z]
            return 3;
        case 0x83A5: // [C-Up]
            return 6;
        case 0x83A6: // [C-Down]
        case 0x83A7: // [C-Left]
            return 8;
        case 0x83A8: // [C-Right]
            return 9;
        case 0x83A9: // [Z-target]
            return 10;
        case 0x83AA: // [Control Stick]
            return 15;
        default:
            return 1;
    }
}

// SoH adapter state used to align a corpus page with the textbox currently
// decoded by the native message engine. The value is 1-based; zero means no
// page has been decoded yet.
uint16_t JPAssist_GetNativeTextBoxNumber(void);

// Stable target bounds for the currently decoded native textbox. Adapters
// report their own logical coordinate space; the overlay normalizes it before
// applying the shared collision-aware placement policy.
bool JPAssist_GetNativeTextboxBounds(JPAssistNativeTextboxBounds* bounds);

#ifdef __cplusplus
}
#endif
