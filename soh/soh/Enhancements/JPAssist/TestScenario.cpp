#include "TestScenario.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

namespace JPAssist {
namespace {

std::vector<TestScenario> sScenarios;
std::string sLoadError;

} // namespace

bool TestScenario_LoadManifest(const std::string& explicitPath) {
    sLoadError.clear();
    sScenarios.clear();
    const std::string path = explicitPath.empty()
                                 ? Ship::Context::LocateFileAcrossAppDirs("jp_assist/test_scenarios.json")
                                 : explicitPath;
    try {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("scenario manifest not found at " + path);
        }
        std::ifstream stream(path);
        nlohmann::json root;
        stream >> root;
        sScenarios = TestScenario_ParseManifest(root);
        SPDLOG_INFO("[JPAssist] Loaded {} Test Lab scenarios from {}", sScenarios.size(), path);
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
