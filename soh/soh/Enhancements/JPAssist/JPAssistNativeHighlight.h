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

#ifdef __cplusplus
}
#endif
