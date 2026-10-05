#pragma once

#include <array>
#include <cstdint>

#include "PluginCorpus.h"
#include "PluginAudio.h"
#include "PluginLayout.h"
#include "PluginProgress.h"
#include "PluginSync.h"
#include "StudySession.h"

namespace JPAssistPlugin {

class Runtime {
  public:
    bool Initialize(const StudyModHostApi& host);
    void Shutdown();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    bool IsCorpusLoaded() const;

    void OnDialogue(const StudyModDialogueEvent& event);
    uint64_t OnInput(const StudyModInputEvent& event);
    void OnLifecycle(const StudyModLifecycleEvent& event);
    int32_t OnNativeHighlight(uint64_t dialogueId, StudyModNativeHighlight& highlight);
    void OnOverlay(const StudyModOverlayFrame& frame, const StudyModOverlayDrawApi& draw);

    int SelectedTokenIndex() const;
    bool DefinitionVisible() const;
    bool SelectedTokenSaved() const;
    bool SelectedTokenKnown() const;
    bool SelectedTokenHasAudio() const;
    int SelectedTokenEncounterCount() const;
    size_t PendingSyncEventCount() const;

  private:
    const Page* CurrentPage() const;
    const Token* CurrentToken() const;
    void Retarget(uint64_t dialogueId, uint32_t pageIndex, uint32_t flags, const StudyModRect& textboxBounds);
    void RefreshEnabledSetting();
    void PublishDiagnostics();

    const StudyModHostApi* mHost = nullptr;
    Corpus mCorpus;
    Audio mAudio;
    LayoutController mLayout;
    Progress mProgress;
    SyncOutbox mSync;
    JPAssist::StudySession mSession;
    uint64_t mDialogueId = 0;
    uint32_t mPageIndex = 0;
    int32_t mChoiceIndex = -1;
    StudyModRect mTextboxBounds{};
    float mScrollPosition = 0.0f;
    bool mEnabled = false;
    std::array<int32_t, 20> mPublishedDiagnostics{};
    bool mHasPublishedDiagnostics = false;
};

} // namespace JPAssistPlugin
