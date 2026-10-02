#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "TestScenario.h"

TEST(TestScenarioParser, AcceptsHexAndPosition) {
    const nlohmann::json manifest = {
        { "scenarios", { {
            { "id", "forest" }, { "entrance", "0x00BB" }, { "textId", "0x1001" },
            { "language", "jpn" }, { "position", {
                { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "yaw", "0x4000" }
            } }, { "expected", { { "pages", 2 }, { "tokens", 4 } } }
        } } }
    };

    const auto scenarios = JPAssist::TestScenario_ParseManifest(manifest);
    ASSERT_EQ(scenarios.size(), 1);
    EXPECT_EQ(scenarios[0].entrance, 0x00BB);
    EXPECT_EQ(scenarios[0].textId, 0x1001);
    EXPECT_EQ(scenarios[0].language, 3);
    EXPECT_TRUE(scenarios[0].position.enabled);
    EXPECT_EQ(scenarios[0].position.yaw, 0x4000);
    EXPECT_EQ(scenarios[0].expected.pages, 2);
}

TEST(TestScenarioParser, RejectsMissingScenarioArray) {
    EXPECT_THROW(JPAssist::TestScenario_ParseManifest(nlohmann::json::object()), std::invalid_argument);
}

TEST(TestScenarioParser, RejectsUnknownLanguage) {
    const nlohmann::json manifest = { { "scenarios", { {
        { "id", "bad" }, { "entrance", 1 }, { "textId", 2 }, { "language", "hylian" }
    } } } };
    EXPECT_THROW(JPAssist::TestScenario_ParseManifest(manifest), std::invalid_argument);
}
