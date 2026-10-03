#include <gtest/gtest.h>

#include "JPAssistAudioMath.h"

TEST(JPAssistAudioMath, ResamplesMonoAndDuplicatesItToStereo) {
    const float source[] = { 0.0f, 1.0f };
    const auto output = JPAssist::JPAssistAudio_ResampleToStereo(source, 2, 1, 16000, 32000);
    ASSERT_EQ(output.size(), 8);
    EXPECT_EQ(output[0], output[1]);
    EXPECT_NEAR(output[2], 16384, 1);
    EXPECT_EQ(output[2], output[3]);
    EXPECT_EQ(output[4], 32767);
    EXPECT_EQ(output[4], output[5]);
}

TEST(JPAssistAudioMath, MixClampsInsteadOfOverflowing) {
    EXPECT_EQ(JPAssist::JPAssistAudio_MixSample(30000, 30000, 1.0f), 32767);
    EXPECT_EQ(JPAssist::JPAssistAudio_MixSample(-30000, -30000, 1.0f), -32768);
    EXPECT_EQ(JPAssist::JPAssistAudio_MixSample(1000, 2000, 0.5f), 2000);
}
