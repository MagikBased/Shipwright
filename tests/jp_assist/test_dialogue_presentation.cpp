#include <gtest/gtest.h>

#include "DialoguePresentation.h"

using DialogueStudy::DialogueDisplayMode;
using DialogueStudy::DialogueSurface;
using DialogueStudy::AllowsOrdinaryDialogueToggle;
using DialogueStudy::PresentationCapabilities;
using DialogueStudy::ResolvePresentationPlan;

TEST(DialoguePresentation, UsesNativeTextboxWhenHostSupportsIt) {
    PresentationCapabilities capabilities;
    capabilities.nativeTextReplacement = true;

    const auto plan = ResolvePresentationPlan(DialogueDisplayMode::NativeSwap, capabilities);

    EXPECT_EQ(plan.surface, DialogueSurface::NativeTextbox);
    EXPECT_FALSE(plan.usedFallback);
}

TEST(DialoguePresentation, NativeSwapFallsBackToAttachedPanel) {
    const auto plan = ResolvePresentationPlan(DialogueDisplayMode::NativeSwap, PresentationCapabilities{});

    EXPECT_EQ(plan.surface, DialogueSurface::AttachedPanel);
    EXPECT_TRUE(plan.usedFallback);
}

TEST(DialoguePresentation, AttachedTranslationUsesPanel) {
    const auto plan = ResolvePresentationPlan(DialogueDisplayMode::AttachedTranslation, PresentationCapabilities{});

    EXPECT_EQ(plan.surface, DialogueSurface::AttachedPanel);
    EXPECT_FALSE(plan.usedFallback);
}

TEST(DialoguePresentation, JapaneseOnlyKeepsOrdinaryDialogueUnmodified) {
    PresentationCapabilities capabilities;
    capabilities.nativeTextReplacement = true;

    const auto plan = ResolvePresentationPlan(DialogueDisplayMode::JapaneseOnly, capabilities);

    EXPECT_EQ(plan.surface, DialogueSurface::Hidden);
    EXPECT_FALSE(plan.usedFallback);
    EXPECT_FALSE(AllowsOrdinaryDialogueToggle(DialogueDisplayMode::JapaneseOnly));
    EXPECT_TRUE(AllowsOrdinaryDialogueToggle(DialogueDisplayMode::AttachedTranslation));
}
