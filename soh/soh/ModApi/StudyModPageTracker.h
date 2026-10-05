#pragma once

#include <cstdint>

namespace StudyModApi {

// Converts the game's decoded textbox counter into the portable ABI's
// zero-based page index. The first ready observation becomes the baseline
// because debug/custom-message injectors may not reset a game's private
// counter in the same way as its ordinary message-open path.
class PageTracker {
  public:
    void Reset() {
        mPageIndex = 0;
        mBaselineTextBoxNumber = 0;
        mHasBaseline = false;
    }

    bool Observe(uint16_t nativeTextBoxNumber, int& pageIndex) {
        if (nativeTextBoxNumber == 0) {
            pageIndex = mPageIndex;
            return false;
        }

        if (!mHasBaseline || nativeTextBoxNumber < mBaselineTextBoxNumber) {
            mBaselineTextBoxNumber = nativeTextBoxNumber;
            mHasBaseline = true;
            mPageIndex = 0;
            pageIndex = mPageIndex;
            return false;
        }

        const int observedPage = static_cast<int>(nativeTextBoxNumber - mBaselineTextBoxNumber);
        const bool changed = observedPage != mPageIndex;
        mPageIndex = observedPage;
        pageIndex = mPageIndex;
        return changed;
    }

    int GetPageIndex() const {
        return mPageIndex;
    }

  private:
    int mPageIndex = 0;
    uint16_t mBaselineTextBoxNumber = 0;
    bool mHasBaseline = false;
};

} // namespace StudyModApi
