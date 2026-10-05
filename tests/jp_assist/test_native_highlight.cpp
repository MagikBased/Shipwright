#include <gtest/gtest.h>

#include "mods/study_mod_game_bridge.h"

TEST(JPAssistNativeHighlight, ControllerGlyphsAdvanceByNormalizedMarkerLength) {
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x839F), 3U);  // [A]
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x83A5), 6U);  // [C-Up]
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x83A6), 8U);  // [C-Down]
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x83A8), 9U);  // [C-Right]
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x83A9), 10U); // [Z-target]
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x83AA), 15U); // [Control Stick]
    EXPECT_EQ(StudyModHost_GetNormalizedGlyphLength(0x82A0), 1U);  // ordinary kana
}

TEST(JPAssistNativeHighlight, RepeatedStonePromptKeepsSecondLineTokenAligned) {
    // Normalized first line: 石の前で　[A]…\n (10 code points).
    uint32_t normalizedIndex = 4; // after 石の前で
    normalizedIndex += 1;         // ideographic space
    normalizedIndex += StudyModHost_GetNormalizedGlyphLength(0x839F);
    normalizedIndex += 1; // ellipsis
    normalizedIndex += 1; // newline

    EXPECT_EQ(normalizedIndex, 10U);
}
