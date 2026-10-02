#pragma once

#include <cstdint>

#include "DialoguePresentation.h"

namespace JPAssist {

struct RuntimeStatus {
    uint16_t textId = 0xFFFF;
    int pageIndex = 0;
    uint8_t requestedLanguage = 0;
    bool alternateLanguageVisible = false;
    bool studyModeActive = false;
    bool currentPageIsChoice = false;
    bool choiceSelectionFrozen = false;
    uint8_t choiceIndex = 0;
    int selectedTokenIndex = 0;
    int currentPageTokenCount = 0;
    bool selectedTokenSaved = false;
    DialogueStudy::DialogueDisplayMode displayMode = DialogueStudy::DialogueDisplayMode::AttachedTranslation;
    DialogueStudy::DialogueSurface dialogueSurface = DialogueStudy::DialogueSurface::Hidden;
    bool displayModeFallback = false;
    uint64_t languageToggleCount = 0;
    uint64_t studyEnterCount = 0;
    uint64_t studyNavigationCount = 0;
    uint64_t studyScrollCount = 0;
    uint64_t saveToggleCount = 0;
};

RuntimeStatus JPAssist_GetRuntimeStatus();

// Test Lab seam: queues controller state for the next dialogue update. The
// manager applies it to the real Input object immediately before running the
// same production handlers used by physical controllers.
void JPAssist_QueueTestInput(uint16_t buttons, int8_t stickY = 0, bool hasStickY = false);

} // namespace JPAssist
