#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace JPAssist {

// Remembers an occurrence index independently for every dialogue page. This is
// intentionally process-local UI state: closing and reopening Study Mode
// restores the same word, without adding transient cursor positions to player
// progress or save data.
class StudySelectionMemory {
  public:
    void Remember(uint16_t textId, int pageIndex, int tokenIndex, int tokenCount) {
        if (tokenCount <= 0) {
            return;
        }
        mSelections[Key(textId, pageIndex)] = std::clamp(tokenIndex, 0, tokenCount - 1);
    }

    int Restore(uint16_t textId, int pageIndex, int tokenCount) const {
        if (tokenCount <= 0) {
            return 0;
        }
        const auto found = mSelections.find(Key(textId, pageIndex));
        return std::clamp(found == mSelections.end() ? 0 : found->second, 0, tokenCount - 1);
    }

  private:
    static uint64_t Key(uint16_t textId, int pageIndex) {
        return (static_cast<uint64_t>(textId) << 32) | static_cast<uint32_t>(pageIndex);
    }

    std::unordered_map<uint64_t, int> mSelections;
};

} // namespace JPAssist
