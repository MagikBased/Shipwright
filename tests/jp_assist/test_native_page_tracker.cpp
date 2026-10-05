#include <gtest/gtest.h>

#include "soh/ModApi/StudyModPageTracker.h"

TEST(NativePageTracker, IgnoresUndecodedPageAndTracksDecodedPages) {
    StudyModApi::PageTracker tracker;
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
    StudyModApi::PageTracker tracker;
    int pageIndex = -1;

    EXPECT_FALSE(tracker.Observe(1, pageIndex));
    EXPECT_TRUE(tracker.Observe(2, pageIndex));
    EXPECT_FALSE(tracker.Observe(2, pageIndex));
    EXPECT_EQ(pageIndex, 1);
}

TEST(NativePageTracker, ResetReturnsToFirstCorpusPage) {
    StudyModApi::PageTracker tracker;
    int pageIndex = -1;

    ASSERT_FALSE(tracker.Observe(1, pageIndex));
    ASSERT_TRUE(tracker.Observe(2, pageIndex));
    tracker.Reset();

    EXPECT_EQ(tracker.GetPageIndex(), 0);
    EXPECT_FALSE(tracker.Observe(1, pageIndex));
    EXPECT_EQ(pageIndex, 0);
}

TEST(NativePageTracker, UsesFirstReadyObservationAsBaseline) {
    StudyModApi::PageTracker tracker;
    int pageIndex = -1;

    // MessageViewer's debug injector leaves the private SoH counter running
    // across messages, so a newly injected message may begin at (for example)
    // native textbox 7 rather than 1.
    EXPECT_FALSE(tracker.Observe(7, pageIndex));
    EXPECT_EQ(pageIndex, 0);
    EXPECT_TRUE(tracker.Observe(8, pageIndex));
    EXPECT_EQ(pageIndex, 1);
}
