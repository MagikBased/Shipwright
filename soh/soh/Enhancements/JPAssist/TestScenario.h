#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace JPAssist {

struct TestPosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int16_t yaw = 0;
    bool enabled = false;
};

struct TestExpectation {
    int pages = -1;
    int tokens = -1;
    int choiceCount = -1;
};

struct TestScenario {
    std::string id;
    std::string label;
    std::string description;
    std::string progressionProfile = "debug_child";
    int32_t entrance = -1;
    int8_t room = 0;
    std::string age = "child";
    std::string time = "day";
    uint16_t textId = 0;
    uint8_t language = 0;
    TestPosition position;
    TestExpectation expected;
};

// Pure parser used by both the runtime Test Lab and the standalone unit tests.
std::vector<TestScenario> TestScenario_ParseManifest(const nlohmann::json& root);

bool TestScenario_LoadManifest(const std::string& explicitPath = "");
const std::vector<TestScenario>& TestScenario_GetAll();
const TestScenario* TestScenario_Find(const std::string& id);
const std::string& TestScenario_GetLoadError();

} // namespace JPAssist
