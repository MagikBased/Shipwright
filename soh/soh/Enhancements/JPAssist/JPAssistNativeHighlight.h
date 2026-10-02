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

// C-compatible render bridge. The native message renderer asks only for the
// selected normalized-text span; it remains independent of corpus and study
// state implementation details.
bool JPAssist_GetNativeHighlight(uint16_t textId, JPAssistNativeHighlight* highlight);

// SoH adapter state used to align a corpus page with the textbox currently
// decoded by the native message engine. The value is 1-based; zero means no
// page has been decoded yet.
uint16_t JPAssist_GetNativeTextBoxNumber(void);

#ifdef __cplusplus
}
#endif
