#pragma once

#include <cstdint>

namespace JPAssist {

// Converts Ship of Harkinian's decoded textbox counter into the corpus's
// 0-based page index. The first ready observation becomes the baseline
// because debug/custom-message injectors do not necessarily reset SoH's
// private counter the way Message_OpenText does.
class NativePageTracker {
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

} // namespace JPAssist
