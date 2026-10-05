#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "study_mod/study_mod_api.h"

namespace JPAssistPlugin {

class Progress {
  public:
    bool Initialize(const StudyModHostApi& host, const char* modId);
    bool Load();
    bool Save();
    bool ToggleSaved(const std::string& tokenId);
    bool MarkKnown(const std::string& tokenId, const std::string& senseId);
    bool IsSaved(const std::string& tokenId) const;
    bool IsKnown(const std::string& tokenId, const std::string& senseId) const;
    void RecordEncounter(const std::string& tokenId);
    int EncounterCount(const std::string& tokenId) const;

  private:
    std::string mPath;
    std::set<std::string> mSaved;
    std::set<std::pair<std::string, std::string>> mKnown;
    std::unordered_map<std::string, int> mEncounters;
    std::unordered_map<std::string, int64_t> mLastEncounter;
    bool mDirty = false;
};

} // namespace JPAssistPlugin
