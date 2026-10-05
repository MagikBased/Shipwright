#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mods/study_mod_api.h"

namespace JPAssistPlugin {

struct Token {
    std::string surface;
    std::string lemma;
    std::string reading;
    std::string dictionaryReading;
    std::string partOfSpeech;
    std::string meaning;
    std::string note;
    std::string senseId;
    uint32_t start = 0;
    uint32_t length = 0;

    std::string Id() const;
};

struct Page {
    std::string japanese;
    std::string english;
    bool isChoice = false;
    std::vector<Token> tokens;
};

class Corpus {
  public:
    bool LoadFromHost(const StudyModHostApi& host, const char* resourceName);
    bool LoadJson(const std::string& contents);
    const Page* FindPage(uint64_t dialogueId, uint32_t pageIndex) const;
    bool IsLoaded() const;
    const std::string& Error() const;
    const std::string& Version() const;

  private:
    std::unordered_map<uint64_t, std::vector<Page>> mPages;
    std::string mError;
    std::string mVersion = "unavailable";
};

} // namespace JPAssistPlugin
