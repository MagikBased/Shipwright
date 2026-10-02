#include <gtest/gtest.h>

#include "NativePageTracker.h"

TEST(NativePageTracker, IgnoresUndecodedPageAndTracksDecodedPages) {
    JPAssist::NativePageTracker tracker;
    int pageIndex = -1;

    EXPECT_FALSE(tracker.Observe(0, pageIndex));
    EXPECT_EQ(pageIndex, 0);
    EXPECT_FALSE(tracker.Observe(1, pageIndex));
    EXPECT_EQ(pageIndex, 0);
    EXPECT_TRUE(tracker.Observe(2, pageIndex));
    EXPECT_EQ(pageIndex, 1);
    EXPECT_TRUE(tracker.Observe(3, pageIndex));
    EXPECT_EQ(pageIndex, 2);
}

TEST(NativePageTracker, SameDecodedPageDoesNotAdvanceTwice) {
    JPAssist::NativePageTracker tracker;
    int pageIndex = -1;

    EXPECT_FALSE(tracker.Observe(1, pageIndex));
    EXPECT_TRUE(tracker.Observe(2, pageIndex));
    EXPECT_FALSE(tracker.Observe(2, pageIndex));
    EXPECT_EQ(pageIndex, 1);
}

TEST(NativePageTracker, ResetReturnsToFirstCorpusPage) {
    JPAssist::NativePageTracker tracker;
    int pageIndex = -1;

    ASSERT_FALSE(tracker.Observe(1, pageIndex));
    ASSERT_TRUE(tracker.Observe(2, pageIndex));
    tracker.Reset();

    EXPECT_EQ(tracker.GetPageIndex(), 0);
    EXPECT_FALSE(tracker.Observe(1, pageIndex));
    EXPECT_EQ(pageIndex, 0);
}

TEST(NativePageTracker, UsesFirstReadyObservationAsBaseline) {
    JPAssist::NativePageTracker tracker;
    int pageIndex = -1;

    // MessageViewer's debug injector leaves the private SoH counter running
    // across messages, so a newly injected message may begin at (for example)
    // native textbox 7 rather than 1.
    EXPECT_FALSE(tracker.Observe(7, pageIndex));
    EXPECT_EQ(pageIndex, 0);
    EXPECT_TRUE(tracker.Observe(8, pageIndex));
    EXPECT_EQ(pageIndex, 1);
}
