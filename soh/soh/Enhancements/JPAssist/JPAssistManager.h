#pragma once

#include <cstdint>

namespace JPAssist {

struct RuntimeStatus {
    uint16_t textId = 0xFFFF;
    int pageIndex = 0;
    uint8_t requestedLanguage = 0;
    bool alternateLanguageVisible = false;
    bool studyModeActive = false;
    int selectedTokenIndex = 0;
    int currentPageTokenCount = 0;
    uint64_t languageToggleCount = 0;
    uint64_t studyEnterCount = 0;
};

RuntimeStatus JPAssist_GetRuntimeStatus();

} // namespace JPAssist
