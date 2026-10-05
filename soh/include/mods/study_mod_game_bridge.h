#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Stable textbox bounds in the game's logical coordinate space. */
typedef struct StudyModNativeTextboxBounds {
    int16_t y;
    int16_t height;
    int16_t logical_screen_height;
} StudyModNativeTextboxBounds;

/**
 * Returns the length of a decoded game glyph in the normalized dialogue text
 * consumed by study mods. OoT controller glyphs expand to bracketed labels.
 */
static inline uint32_t StudyModHost_GetNormalizedGlyphLength(uint16_t character) {
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

/** The currently decoded 1-based page number; zero means no page is ready. */
uint16_t StudyModHost_GetNativeTextBoxNumber(void);

/** Returns stable bounds for the currently decoded native textbox. */
bool StudyModHost_GetNativeTextboxBounds(StudyModNativeTextboxBounds* bounds);

#ifdef __cplusplus
}
#endif
