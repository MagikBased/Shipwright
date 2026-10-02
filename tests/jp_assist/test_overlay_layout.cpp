#include <array>

#include <gtest/gtest.h>

#include "JPAssistOverlayLayout.h"

namespace {

struct Resolution {
    float width;
    float height;
};

TEST(JPAssistOverlayLayout, RemainsInsideRepresentativeViewports) {
    constexpr std::array<Resolution, 3> resolutions = { {
        { 1280.0f, 720.0f },
        { 1920.0f, 1080.0f },
        { 3440.0f, 1440.0f },
    } };
    constexpr std::array<float, 3> scales = { 0.7f, 1.0f, 1.5f };

    for (const Resolution resolution : resolutions) {
        for (const float scale : scales) {
            const JPAssist::OverlayLayout layout =
                JPAssist::JPAssistOverlay_ComputeLayout(0.0f, 0.0f, resolution.width, resolution.height, scale);
            EXPECT_GT(layout.width, 0.0f);
            EXPECT_GT(layout.height, 0.0f);
            EXPECT_GE(layout.x, 0.0f);
            EXPECT_GE(layout.y, 0.0f);
            EXPECT_LE(layout.x + layout.width, resolution.width);
            EXPECT_LE(layout.y + layout.height, resolution.height);
        }
    }
}

TEST(JPAssistOverlayLayout, KeepsWideStudyCardBottomCenteredAt720p) {
    const JPAssist::OverlayLayout layout =
        JPAssist::JPAssistOverlay_ComputeLayout(0.0f, 0.0f, 1280.0f, 720.0f, 1.5f);

    EXPECT_LE(layout.height, 130.0f * 1.5f);
    EXPECT_FLOAT_EQ(layout.x, (1280.0f - layout.width) * 0.5f);
    EXPECT_LE(layout.y + layout.height, 720.0f);
}

TEST(JPAssistOverlayLayout, HonorsOffsetWorkAreaAndSanitizesScale) {
    const JPAssist::OverlayLayout layout =
        JPAssist::JPAssistOverlay_ComputeLayout(100.0f, 50.0f, 800.0f, 600.0f, NAN);

    EXPECT_GE(layout.x, 100.0f);
    EXPECT_GE(layout.y, 50.0f);
    EXPECT_LE(layout.x + layout.width, 900.0f);
    EXPECT_LE(layout.y + layout.height, 650.0f);
    EXPECT_EQ(layout.scale, 1.0f);
}

TEST(JPAssistOverlayLayout, HandlesMinimalWorkArea) {
    const JPAssist::OverlayLayout layout =
        JPAssist::JPAssistOverlay_ComputeLayout(10.0f, 20.0f, 1.0f, 1.0f, 1.5f);

    EXPECT_GE(layout.x, 10.0f);
    EXPECT_GE(layout.y, 20.0f);
    EXPECT_LE(layout.x + layout.width, 11.0f);
    EXPECT_LE(layout.y + layout.height, 21.0f);
}

TEST(JPAssistOverlayLayout, MovesAboveLowerDialogueWithHudClearance) {
    constexpr float height = 1080.0f;
    constexpr float dialogueTop = 142.0f / 240.0f;
    constexpr float dialogueBottom = 206.0f / 240.0f;
    const JPAssist::OverlayLayout baseline =
        JPAssist::JPAssistOverlay_ComputeLayout(0.0f, 0.0f, 1920.0f, height, 1.0f);
    const JPAssist::OverlayLayout layout = JPAssist::JPAssistOverlay_ComputeAdaptiveLayout(
        0.0f, 0.0f, 1920.0f, height, 1.0f, true, dialogueTop, dialogueBottom);

    EXPECT_LT(layout.y, baseline.y);
    EXPECT_GE(layout.y, height * 0.22f);
    EXPECT_LE(layout.y + layout.height, dialogueTop * height - 12.0f);
}

TEST(JPAssistOverlayLayout, KeepsBottomPlacementForUpperDialogue) {
    const JPAssist::OverlayLayout baseline =
        JPAssist::JPAssistOverlay_ComputeLayout(0.0f, 0.0f, 1920.0f, 1080.0f, 1.0f);
    const JPAssist::OverlayLayout layout = JPAssist::JPAssistOverlay_ComputeAdaptiveLayout(
        0.0f, 0.0f, 1920.0f, 1080.0f, 1.0f, true, 38.0f / 240.0f, 102.0f / 240.0f);

    EXPECT_FLOAT_EQ(layout.y, baseline.y);
}

TEST(JPAssistOverlayLayout, IgnoresInvalidDialogueBounds) {
    const JPAssist::OverlayLayout baseline =
        JPAssist::JPAssistOverlay_ComputeLayout(0.0f, 0.0f, 1280.0f, 720.0f, 1.0f);
    const JPAssist::OverlayLayout layout = JPAssist::JPAssistOverlay_ComputeAdaptiveLayout(
        0.0f, 0.0f, 1280.0f, 720.0f, 1.0f, true, NAN, 1.0f);

    EXPECT_FLOAT_EQ(layout.y, baseline.y);
}

} // namespace
