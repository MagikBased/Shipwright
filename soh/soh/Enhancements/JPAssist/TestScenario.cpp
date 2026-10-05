#include "TestScenario.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "JPAssistHost.h"

namespace JPAssist {
namespace {

std::vector<TestScenario> sScenarios;
std::string sLoadError;

} // namespace

bool TestScenario_LoadManifest(const std::string& explicitPath) {
    sLoadError.clear();
    sScenarios.clear();
    try {
        nlohmann::json root;
        std::string sourceLabel;
        if (explicitPath.empty()) {
            std::string contents;
            if (!JPAssistHost_ReadDataFile("jp_assist/test_scenarios.json", contents)) {
                throw std::runtime_error("scenario manifest not found in host data");
            }
            root = nlohmann::json::parse(contents);
            sourceLabel = "host data jp_assist/test_scenarios.json";
        } else {
            if (!std::filesystem::exists(explicitPath)) {
                throw std::runtime_error("scenario manifest not found at " + explicitPath);
            }
            std::ifstream stream(explicitPath);
            stream >> root;
            sourceLabel = explicitPath;
        }
        sScenarios = TestScenario_ParseManifest(root);
        SPDLOG_INFO("[JPAssist] Loaded {} Test Lab scenarios from {}", sScenarios.size(), sourceLabel);
        return true;
    } catch (const std::exception& exception) {
        sLoadError = exception.what();
        SPDLOG_WARN("[JPAssist] Test Lab scenarios unavailable: {}", sLoadError);
        return false;
    }
}

const std::vector<TestScenario>& TestScenario_GetAll() {
    return sScenarios;
}

const TestScenario* TestScenario_Find(const std::string& id) {
    for (const auto& scenario : sScenarios) {
        if (scenario.id == id) {
            return &scenario;
        }
    }
    return nullptr;
}

const std::string& TestScenario_GetLoadError() {
    return sLoadError;
}

} // namespace JPAssist
