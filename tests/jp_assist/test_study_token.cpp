#include <gtest/gtest.h>

#include "StudyRepository.h"

TEST(StudyToken, StableIdUsesDictionaryReading) {
    JPAssist::StudyToken token;
    token.lemma = "行く";
    token.reading = "いった";
    token.dictionaryReading = "いく";
    EXPECT_EQ(token.Id(), "行く|いく");
}

TEST(StudyToken, StableIdFallsBackToSurfaceReading) {
    JPAssist::StudyToken token;
    token.lemma = "妖精";
    token.reading = "ようせい";
    EXPECT_EQ(token.Id(), "妖精|ようせい");
}
