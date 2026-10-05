#pragma once

#include <cstdint>

#include "StudySelectionMemory.h"

namespace JPAssist {

enum class StudyCommand : uint32_t {
    None = 0,
    OpenWithDefinition = 1U << 0,
    OpenRecallFirst = 1U << 1,
    Close = 1U << 2,
    ToggleDefinition = 1U << 3,
    PreviousWord = 1U << 4,
    NextWord = 1U << 5,
    ScrollUp = 1U << 6,
    ScrollDown = 1U << 7,
    ToggleSaved = 1U << 8,
    MarkKnown = 1U << 9,
    PlayAudio = 1U << 10,
};

constexpr StudyCommand operator|(StudyCommand left, StudyCommand right) {
    return static_cast<StudyCommand>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr bool HasStudyCommand(StudyCommand commands, StudyCommand command) {
    return (static_cast<uint32_t>(commands) & static_cast<uint32_t>(command)) != 0;
}

struct StudySessionResult {
    bool entered = false;
    bool exited = false;
    bool definitionToggled = false;
    bool selectionChanged = false;
    bool toggleSaved = false;
    bool markKnown = false;
    bool playAudio = false;
    bool consumeEntryCommands = false;
    bool consumeStudyCommands = false;
    bool freezeChoice = false;
    uint8_t frozenChoiceIndex = 0;
    float scrollPixels = 0.0f;
};

struct StudySessionCounters {
    uint64_t enter = 0;
    uint64_t navigation = 0;
    uint64_t scroll = 0;
    uint64_t saveToggle = 0;
    uint64_t knownMark = 0;
    uint64_t definitionToggle = 0;
    uint64_t audioPlay = 0;
};

// Game-neutral state machine for Study Mode. The game adapter translates its
// controller representation into StudyCommand values and performs the side
// effects requested by StudySessionResult.
class StudySession {
  public:
    void RetargetDialogue(uint16_t textId, int pageIndex, int tokenCount, bool isChoice);
    bool Exit();
    bool ClearDialogue();
    StudySessionResult HandleCommands(StudyCommand commands, uint8_t choiceIndex);

    void RecordKnownMarked();
    void RecordAudioPlayed();

    uint16_t TextId() const;
    int PageIndex() const;
    int TokenCount() const;
    int SelectedTokenIndex() const;
    bool IsActive() const;
    bool IsDefinitionVisible() const;
    bool IsChoicePage() const;
    bool IsChoiceFrozen() const;
    uint8_t FrozenChoiceIndex() const;
    const StudySessionCounters& Counters() const;

  private:
    void RememberSelection();
    void RestoreSelection();

    uint16_t mTextId = 0xFFFF;
    int mPageIndex = 0;
    int mTokenCount = 0;
    int mSelectedTokenIndex = 0;
    bool mChoicePage = false;
    bool mActive = false;
    bool mDefinitionVisible = true;
    bool mChoiceFrozen = false;
    uint8_t mFrozenChoiceIndex = 0;
    StudySelectionMemory mSelectionMemory;
    StudySessionCounters mCounters;
};

} // namespace JPAssist
