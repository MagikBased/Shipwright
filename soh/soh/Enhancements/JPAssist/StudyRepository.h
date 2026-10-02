#pragma once

#include <string>
#include <cstdint>
#include <vector>

// Runtime-facing normalized corpus types. The corpus is generated locally
// from the user's archive; complete extracted dialogue is never tracked.

namespace JPAssist {

struct StudyToken {
    std::string surface;       // as it would appear in the dialogue
    std::string lemma;         // dictionary form
    std::string reading;       // hiragana reading
    std::string dictionaryReading; // reading of lemma; stable vocabulary identity
    std::string partOfSpeech;
    std::string meaning;       // context-appropriate English meaning
    std::string note;          // optional usage note
    uint32_t start = 0;        // Unicode code-point offset in the normalized page
    uint32_t length = 0;       // Unicode code-point length

    // Stable ID for persistence (design doc section 8.3: "Stable IDs should
    // derive from lemma, reading, and selected sense rather than list
    // position"). Derived rather than stored, so it can't drift out of
    // sync with the fields it's derived from.
    std::string Id() const {
        return lemma + "|" + (dictionaryReading.empty() ? reading : dictionaryReading);
    }
};

struct StudyPage {
    std::string japanese;
    std::string english;
    bool isChoice = false;
    int choiceCount = 0;
    std::vector<StudyToken> tokens;
};

// Loads scripts/jp_assist/tokenize_dialogue.py's runtime_data.json. The
// default search path is jp_assist/runtime_data.json across Ship's app dirs;
// an explicit path is useful for development and future mod packages.
bool StudyRepository_LoadCorpus(const std::string& explicitPath = "");
bool StudyRepository_IsCorpusLoaded();
const std::string& StudyRepository_GetCorpusVersion();
const std::string& StudyRepository_GetLoadError();

// Returns nullptr when the message/page has no reviewed corpus data. Page
// indices are clamped to support the design's JP/EN page-alignment fallback.
const StudyPage* StudyRepository_FindPage(uint16_t textId, int pageIndex);
size_t StudyRepository_GetPageCount(uint16_t textId);

// Fixed fallback tokens retained only for UI development when a generated
// corpus is unavailable. Production Study Mode does not silently substitute
// them for unrelated dialogue.
const std::vector<StudyToken>& StudyRepository_GetTestTokens();

} // namespace JPAssist
