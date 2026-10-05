#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "study_mod/study_mod_api.h"

namespace JPAssistPlugin {

class Audio {
  public:
    bool Initialize(const StudyModHostApi& host, const char* modId);
    bool HasWord(const std::string& wordId) const;
    bool PlayWord(const std::string& wordId);

  private:
    bool ReadResource(const std::string& name, std::string& output) const;
    bool Decode(const std::string& resource, std::vector<int16_t>& output) const;

    const StudyModHostApi* mHost = nullptr;
    std::string mModId;
    std::unordered_map<std::string, std::string> mResources;
    std::unordered_map<std::string, std::vector<int16_t>> mDecoded;
};

} // namespace JPAssistPlugin
