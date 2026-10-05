#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "LearningSyncClient.h"
#include "PluginCorpus.h"
#include "mods/study_mod_api.h"

namespace JPAssistPlugin {

class SyncOutbox {
  public:
    bool Initialize(const StudyModHostApi& host, const char* modId, std::string contentVersion);
    bool Reload();
    void Shutdown();
    void RefreshEnabled();
    bool RecordDialogue(const std::string& type, uint64_t dialogueId, uint32_t pageIndex);
    bool RecordWord(const std::string& type, const Token& token, uint64_t dialogueId, uint32_t pageIndex,
                    uint32_t count = 1);
    size_t PendingCount() const;

  private:
    JPAssist::LearningEvent BaseEvent(const std::string& type, uint64_t dialogueId, uint32_t pageIndex) const;

    const StudyModHostApi* mHost = nullptr;
    std::string mModId;
    std::string mHostId;
    std::string mGameId;
    std::string mContentVersion;
    std::string mPath;
    std::unique_ptr<JPAssist::LearningSyncClient> mClient;
    bool mEnabled = false;
    std::string mEndpoint;
    JPAssist::LearningSyncStatus mPublishedStatus;
    bool mHasPublishedStatus = false;
};

} // namespace JPAssistPlugin
