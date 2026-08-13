#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Shared types for the JP Assist technical spike (docs/JP_ASSIST_DESIGN.md,
// Milestone 1). Scope is intentionally limited to what the language-switch
// spike needs: page/choice structure for both languages, plus plain text for
// English specifically. Decoding Japanese glyph codes into displayable text
// is a corpus-pipeline concern (design doc section 7) and is out of scope
// here - this spike only needs to know where Japanese pages/choices fall,
// not render them itself.

namespace JPAssist {

struct DialoguePage {
    // Offset (in the raw segment's native unit - bytes for English, u16
    // units for Japanese) where this page's content starts.
    uint32_t unitOffset = 0;
    // English-only: control-code-stripped plain text for this page. Empty
    // for Japanese pages (see file header note).
    std::string englishText;
    bool isChoice = false;
    int choiceCount = 0; // 2 or 3 when isChoice
};

struct DialogueStructure {
    bool found = false;
    std::vector<DialoguePage> pages;
};

} // namespace JPAssist
