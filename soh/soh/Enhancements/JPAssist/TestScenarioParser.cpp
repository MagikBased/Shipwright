#include "TestScenario.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace JPAssist {
namespace {

int64_t ParseInteger(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return value.get<int64_t>();
    }
    if (value.is_string()) {
        return std::stoll(value.get<std::string>(), nullptr, 0);
    }
    throw std::invalid_argument("expected an integer or base-prefixed integer string");
}

uint8_t ParseLanguage(const std::string& language) {
    if (language == "japanese" || language == "jpn") {
        return 3; // LANGUAGE_JPN
    }
    if (language == "english" || language == "eng") {
        return 0; // LANGUAGE_ENG
    }
    throw std::invalid_argument("language must be japanese/jpn or english/eng");
}

} // namespace

std::vector<TestScenario> TestScenario_ParseManifest(const nlohmann::json& root) {
    if (!root.is_object() || !root.contains("scenarios") || !root["scenarios"].is_array()) {
        throw std::invalid_argument("manifest must contain a scenarios array");
    }

    std::vector<TestScenario> parsed;
    for (const auto& entry : root["scenarios"]) {
        TestScenario scenario;
        scenario.id = entry.at("id").get<std::string>();
        scenario.label = entry.value("label", scenario.id);
        scenario.description = entry.value("description", "");
        scenario.progressionProfile = entry.value("progressionProfile", "debug_child");
        scenario.entrance = static_cast<int32_t>(ParseInteger(entry.at("entrance")));
        scenario.room = static_cast<int8_t>(entry.value("room", 0));
        scenario.age = entry.value("age", "child");
        scenario.time = entry.value("time", "day");
        scenario.textId = static_cast<uint16_t>(ParseInteger(entry.at("textId")));
        scenario.language = ParseLanguage(entry.value("language", "japanese"));
        scenario.validateControls = entry.value("validateControls", true);

        if (entry.contains("position")) {
            const auto& position = entry["position"];
            scenario.position.x = position.at("x").get<float>();
            scenario.position.y = position.at("y").get<float>();
            scenario.position.z = position.at("z").get<float>();
            if (position.contains("yaw")) {
                scenario.position.yaw = static_cast<int16_t>(ParseInteger(position.at("yaw")));
            }
            scenario.position.enabled = true;
        }
        if (entry.contains("expected")) {
            const auto& expected = entry["expected"];
            scenario.expected.pages = expected.value("pages", -1);
            scenario.expected.tokens = expected.value("tokens", -1);
            scenario.expected.choiceCount = expected.value("choiceCount", -1);
        }
        if (scenario.id.empty() || scenario.entrance < 0) {
            throw std::invalid_argument("scenario id must be non-empty and entrance must be non-negative");
        }
        parsed.push_back(std::move(scenario));
    }
    return parsed;
}

} // namespace JPAssist
