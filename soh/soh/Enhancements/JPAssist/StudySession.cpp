#include "StudySession.h"

#include <algorithm>

namespace JPAssist {

void StudySession::RememberSelection() {
    mSelectionMemory.Remember(mTextId, mPageIndex, mSelectedTokenIndex, mTokenCount);
}

void StudySession::RestoreSelection() {
    mSelectedTokenIndex = mSelectionMemory.Restore(mTextId, mPageIndex, mTokenCount);
}

void StudySession::RetargetDialogue(uint16_t textId, int pageIndex, int tokenCount, bool isChoice) {
    if (mActive) {
        RememberSelection();
    }
    mTextId = textId;
    mPageIndex = pageIndex;
    mTokenCount = std::max(tokenCount, 0);
    mChoicePage = isChoice;
    mChoiceFrozen = false;
    RestoreSelection();
}

bool StudySession::Exit() {
    if (!mActive) {
        return false;
    }
    RememberSelection();
    mActive = false;
    mDefinitionVisible = true;
    mChoiceFrozen = false;
    return true;
}

bool StudySession::ClearDialogue() {
    const bool exited = Exit();
    mTextId = 0xFFFF;
    mPageIndex = 0;
    mTokenCount = 0;
    mSelectedTokenIndex = 0;
    mChoicePage = false;
    mChoiceFrozen = false;
    return exited;
}

StudySessionResult StudySession::HandleCommands(StudyCommand commands, uint8_t choiceIndex) {
    StudySessionResult result;
    const bool openVisible = HasStudyCommand(commands, StudyCommand::OpenWithDefinition);
    const bool openHidden = HasStudyCommand(commands, StudyCommand::OpenRecallFirst);

    if (!mActive) {
        if ((openVisible || openHidden) && mTokenCount > 0) {
            mActive = true;
            mDefinitionVisible = openVisible;
            mCounters.enter++;
            RestoreSelection();
            if (mChoicePage) {
                mChoiceFrozen = true;
                mFrozenChoiceIndex = choiceIndex;
            }
            result.entered = true;
            result.consumeEntryCommands = true;
            result.freezeChoice = mChoiceFrozen;
            result.frozenChoiceIndex = mFrozenChoiceIndex;
        }
        return result;
    }

    result.consumeStudyCommands = true;
    if (HasStudyCommand(commands, StudyCommand::Close)) {
        Exit();
        result.exited = true;
        return result;
    }

    if (HasStudyCommand(commands, StudyCommand::ToggleDefinition)) {
        mDefinitionVisible = !mDefinitionVisible;
        mCounters.definitionToggle++;
        result.definitionToggled = true;
    }

    const int previousIndex = mSelectedTokenIndex;
    if (HasStudyCommand(commands, StudyCommand::NextWord)) {
        mSelectedTokenIndex = std::min(mSelectedTokenIndex + 1, std::max(mTokenCount - 1, 0));
    } else if (HasStudyCommand(commands, StudyCommand::PreviousWord)) {
        mSelectedTokenIndex = std::max(mSelectedTokenIndex - 1, 0);
    }
    if (mSelectedTokenIndex != previousIndex) {
        mCounters.navigation++;
        RememberSelection();
        result.selectionChanged = true;
    }

    if (HasStudyCommand(commands, StudyCommand::ScrollUp)) {
        result.scrollPixels = -80.0f;
        mCounters.scroll++;
    } else if (HasStudyCommand(commands, StudyCommand::ScrollDown)) {
        result.scrollPixels = 80.0f;
        mCounters.scroll++;
    }

    result.toggleSaved = HasStudyCommand(commands, StudyCommand::ToggleSaved) && mTokenCount > 0;
    if (result.toggleSaved) {
        mCounters.saveToggle++;
    }
    result.markKnown = HasStudyCommand(commands, StudyCommand::MarkKnown) && mTokenCount > 0;
    result.playAudio = HasStudyCommand(commands, StudyCommand::PlayAudio) && mTokenCount > 0;

    if (mChoicePage) {
        if (!mChoiceFrozen) {
            mChoiceFrozen = true;
            mFrozenChoiceIndex = choiceIndex;
        }
        result.freezeChoice = true;
        result.frozenChoiceIndex = mFrozenChoiceIndex;
    }
    return result;
}

void StudySession::RecordKnownMarked() {
    mCounters.knownMark++;
}

void StudySession::RecordAudioPlayed() {
    mCounters.audioPlay++;
}

uint16_t StudySession::TextId() const { return mTextId; }
int StudySession::PageIndex() const { return mPageIndex; }
int StudySession::TokenCount() const { return mTokenCount; }
int StudySession::SelectedTokenIndex() const { return mSelectedTokenIndex; }
bool StudySession::IsActive() const { return mActive; }
bool StudySession::IsDefinitionVisible() const { return mDefinitionVisible; }
bool StudySession::IsChoicePage() const { return mChoicePage; }
bool StudySession::IsChoiceFrozen() const { return mActive && mChoiceFrozen; }
uint8_t StudySession::FrozenChoiceIndex() const { return mFrozenChoiceIndex; }
const StudySessionCounters& StudySession::Counters() const { return mCounters; }

} // namespace JPAssist
