#include "JPAssistTestLab.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <ship/Context.h>
#include <ship/debug/Console.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "JPAssistManager.h"
#include "StudyRepository.h"
#include "TestScenario.h"

#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "z64.h"
#include "soh/Enhancements/debugger/MessageViewer.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern PlayState* gPlayState;

extern "C" void Sram_InitDebugSave(void);

namespace JPAssist {
namespace {

enum class SmokeStage { Idle, WaitingForScene, WaitingForMessage, Loaded, Passed, Failed };

struct SmokeState {
    SmokeStage stage = SmokeStage::Idle;
    std::string scenarioId;
    std::string detail = "Not run";
    int framesRemaining = 0;
    uint64_t startedAtToggleCount = 0;
    uint64_t startedAtStudyCount = 0;
    bool validate = false;
};

struct SmokeSuiteState {
    bool active = false;
    size_t nextScenario = 0;
    int passed = 0;
    int failed = 0;
    int advanceDelay = 0;
    std::vector<std::string> failedScenarioIds;
};

SmokeState sSmoke;
SmokeSuiteState sSuite;
TestScenario sPendingScenario;
int sSelectedScenario = 0;
int sRawEntrance = 0x00BB;
int sRawTextId = 0x1001;
int sRawLanguage = LANGUAGE_JPN;
int sSelectedProfile = 0;
constexpr const char* kProgressionProfiles[] = { "debug_child", "post_deku_tree", "adult_all_access", "endgame" };
std::shared_ptr<Ship::GuiWindow> sWindow;
bool sSceneInitializedAfterWarp = false;
int sPostSceneDelay = 0;
bool sAutoRunRequested = false;
bool sAutoRunStarted = false;

bool IsTemporarySession() {
    return gPlayState != nullptr && gSaveContext.fileNum == 0xFF;
}

void SetEventFlag(uint16_t flag) {
    gSaveContext.eventChkInf[flag >> 4] |= 1 << (flag & 0xF);
}

bool StartTemporarySession(std::string& error) {
    if (gPlayState == nullptr) {
        error = "Load any game or debug scene before starting a temporary session.";
        return false;
    }
    Sram_InitDebugSave();
    gSaveContext.fileNum = 0xFF;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.cutsceneIndex = 0;
    error.clear();
    SPDLOG_INFO("[JPAssist Test Lab] Started non-persistent debug session");
    return true;
}

bool ApplyProgressionProfile(const std::string& profile, std::string& error) {
    if (!IsTemporarySession()) {
        error = "Progression profiles require a temporary debug session (file 0xFF).";
        return false;
    }

    // Reinitialize before every profile so scenarios are deterministic and
    // cannot inherit event flags from a previously loaded profile.
    Sram_InitDebugSave();
    gSaveContext.fileNum = 0xFF;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.cutsceneIndex = 0;

    if (profile == "debug_child") {
        gSaveContext.linkAge = LINK_AGE_CHILD;
    } else if (profile == "post_deku_tree") {
        gSaveContext.linkAge = LINK_AGE_CHILD;
        SetEventFlag(EVENTCHKINF_MET_DEKU_TREE);
        SetEventFlag(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD);
        SetEventFlag(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD_DEKU_TREE_DEAD);
    } else if (profile == "adult_all_access") {
        gSaveContext.linkAge = LINK_AGE_ADULT;
        SetEventFlag(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD);
        SetEventFlag(EVENTCHKINF_OBTAINED_ZELDAS_LETTER);
        SetEventFlag(EVENTCHKINF_OBTAINED_OCARINA_OF_TIME);
        SetEventFlag(EVENTCHKINF_OPENED_THE_DOOR_OF_TIME);
        SetEventFlag(EVENTCHKINF_PULLED_MASTER_SWORD_FROM_PEDESTAL);
        SetEventFlag(EVENTCHKINF_ENTERED_MASTER_SWORD_CHAMBER);
    } else if (profile == "endgame") {
        gSaveContext.linkAge = LINK_AGE_ADULT;
        SetEventFlag(EVENTCHKINF_PULLED_MASTER_SWORD_FROM_PEDESTAL);
        SetEventFlag(EVENTCHKINF_USED_FOREST_TEMPLE_BLUE_WARP);
        SetEventFlag(EVENTCHKINF_USED_FIRE_TEMPLE_BLUE_WARP);
        SetEventFlag(EVENTCHKINF_USED_WATER_TEMPLE_BLUE_WARP);
        SetEventFlag(EVENTCHKINF_RAINBOW_BRIDGE_BUILT);
    } else {
        error = "Unknown progression profile: " + profile;
        return false;
    }
    error.clear();
    return true;
}

bool WarpTo(const TestScenario& scenario, std::string& error) {
    if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        error = "A running PlayState is required to warp.";
        return false;
    }

    gSaveContext.linkAge = scenario.age == "adult" ? LINK_AGE_ADULT : LINK_AGE_CHILD;
    gSaveContext.nightFlag = scenario.time == "night";
    gSaveContext.skyboxTime = gSaveContext.dayTime = scenario.time == "night" ? 0xC000 : 0x8000;
    gPlayState->nextEntranceIndex = scenario.entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = scenario.entrance;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = scenario.room;
    if (scenario.position.enabled) {
        gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = { scenario.position.x, scenario.position.y, scenario.position.z };
        gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = scenario.position.yaw;
        gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0xDFF;
        gSaveContext.respawnFlag = 1;
    } else {
        gSaveContext.respawnFlag = 0;
    }
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
    error.clear();
    return true;
}

void WriteSmokeResult(const TestScenario& scenario, bool passed, const std::string& detail) {
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("jp_assist_smoke_results.json");
    nlohmann::json root = nlohmann::json::object();
    try {
        if (std::filesystem::exists(path)) {
            std::ifstream input(path);
            input >> root;
        }
    } catch (...) {
        root = nlohmann::json::object();
    }
    root["schemaVersion"] = 1;
    root["results"][scenario.id] = {
        { "passed", passed },
        { "detail", detail },
        { "textId", fmt::format("{:#06x}", scenario.textId) },
        { "entrance", fmt::format("{:#06x}", scenario.entrance) },
        { "corpusVersion", StudyRepository_GetCorpusVersion() },
    };
    std::ofstream output(path);
    output << root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
}

void WriteSuiteSummary() {
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("jp_assist_smoke_results.json");
    nlohmann::json root = nlohmann::json::object();
    try {
        if (std::filesystem::exists(path)) {
            std::ifstream input(path);
            input >> root;
        }
    } catch (...) {
        root = nlohmann::json::object();
    }
    root["schemaVersion"] = 1;
    root["lastSuite"] = {
        { "passed", sSuite.passed },
        { "failed", sSuite.failed },
        { "total", sSuite.passed + sSuite.failed },
        { "failedScenarioIds", sSuite.failedScenarioIds },
        { "corpusVersion", StudyRepository_GetCorpusVersion() },
    };
    std::ofstream output(path);
    output << root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
}

void FinishSuiteSession(bool passed) {
    if (sAutoRunRequested) {
        SPDLOG_INFO("[JPAssist Test Lab] Automated suite complete: {}", passed ? "PASS" : "FAIL");
        Ship::Context::GetRawInstance()->GetLogger()->flush();
        // This runs inside the game-frame callback. Avoid invoking global C++
        // destructors while Shipwright's engine objects and worker threads are
        // still live; the dedicated automation process has nothing else to
        // preserve after the report and log are flushed.
        std::_Exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    // A full suite repeatedly replaces the debug save and forces scene
    // transitions. It is intentionally disposable; return to File Select so
    // the player cannot continue in that synthetic state. Use Load Scenario
    // when interactive inspection after a test is desired.
    if (gPlayState != nullptr) {
        gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
        gPlayState->state.running = false;
        SET_NEXT_GAMESTATE(&gPlayState->state, FileChoose_Init, FileChooseContext);
    }
}

bool ValidateScenario(const TestScenario& scenario, std::string& detail) {
    const RuntimeStatus runtime = JPAssist_GetRuntimeStatus();
    if (runtime.textId != scenario.textId) {
        detail = fmt::format("Expected text {:#06x}, observed {:#06x}", scenario.textId, runtime.textId);
        return false;
    }
    const StudyPage* page = StudyRepository_FindPage(scenario.textId, runtime.pageIndex);
    if (page == nullptr) {
        detail = "Message opened, but no corpus page was found.";
        return false;
    }
    const size_t pageCount = StudyRepository_GetPageCount(scenario.textId);
    if (scenario.expected.pages >= 0 && static_cast<int>(pageCount) != scenario.expected.pages) {
        detail = fmt::format("Expected {} pages, observed {}", scenario.expected.pages, pageCount);
        return false;
    }
    if (scenario.expected.tokens >= 0 && static_cast<int>(page->tokens.size()) != scenario.expected.tokens) {
        detail = fmt::format("Expected {} tokens, observed {}", scenario.expected.tokens, page->tokens.size());
        return false;
    }
    if (scenario.expected.choiceCount >= 0 && page->choiceCount != scenario.expected.choiceCount) {
        detail = fmt::format("Expected {} choices, observed {}", scenario.expected.choiceCount, page->choiceCount);
        return false;
    }
    detail = fmt::format("PASS: text {:#06x}, {} pages, page {}, {} tokens{}", runtime.textId, pageCount,
                         runtime.pageIndex, page->tokens.size(),
                         page->isChoice ? fmt::format(", {} choices", page->choiceCount) : "");
    return true;
}

bool BeginScenario(const TestScenario& scenario, bool smoke, std::string& error) {
    if (!IsTemporarySession() && !StartTemporarySession(error)) {
        return false;
    }
    if (!ApplyProgressionProfile(scenario.progressionProfile, error) || !WarpTo(scenario, error)) {
        return false;
    }
    sSceneInitializedAfterWarp = false;
    sPostSceneDelay = 0;
    sSmoke.stage = SmokeStage::WaitingForScene;
    sPendingScenario = scenario;
    sSmoke.scenarioId = scenario.id;
    sSmoke.validate = smoke;
    sSmoke.framesRemaining = smoke ? 300 : 120;
    sSmoke.detail = smoke ? "Waiting for scene initialization" : "Warping; message will open after scene initialization";
    const RuntimeStatus runtime = JPAssist_GetRuntimeStatus();
    sSmoke.startedAtToggleCount = runtime.languageToggleCount;
    sSmoke.startedAtStudyCount = runtime.studyEnterCount;
    return true;
}

void FinishScenario(const TestScenario& scenario, bool passed, const std::string& detail) {
    sSmoke.stage = passed ? SmokeStage::Passed : SmokeStage::Failed;
    sSmoke.detail = detail;
    if (sSmoke.validate) {
        WriteSmokeResult(scenario, passed, detail);
    }
    if (sSuite.active) {
        if (passed) {
            sSuite.passed++;
        } else {
            sSuite.failed++;
            sSuite.failedScenarioIds.push_back(scenario.id);
        }
        // Injected messages must finish their normal close path before another
        // scene transition. Warping with an active message can leave animation
        // requests pointing into the outgoing PlayState arena.
        if (gPlayState != nullptr) {
            Message_CloseTextbox(gPlayState);
        }
        sSuite.advanceDelay = 30;
    }
}

bool StartSmokeSuite(std::string& error) {
    const auto& scenarios = TestScenario_GetAll();
    if (scenarios.empty()) {
        error = "No Test Lab scenarios are loaded.";
        return false;
    }
    sSuite = SmokeSuiteState{};
    sSuite.active = true;
    sSuite.nextScenario = 1;
    if (!BeginScenario(scenarios.front(), true, error)) {
        sSuite.active = false;
        return false;
    }
    sSmoke.detail = fmt::format("Suite 1/{}: {}", scenarios.size(), scenarios.front().id);
    return true;
}

bool AdvanceSmokeSuite() {
    if (!sSuite.active || (sSmoke.stage != SmokeStage::Passed && sSmoke.stage != SmokeStage::Failed)) {
        return false;
    }
    if (sSuite.advanceDelay-- > 0) {
        return true;
    }
    if (gPlayState != nullptr && gPlayState->msgCtx.msgMode != MSGMODE_NONE) {
        Message_CloseTextbox(gPlayState);
        return true;
    }

    const auto& scenarios = TestScenario_GetAll();
    if (sSuite.nextScenario >= scenarios.size()) {
        sSuite.active = false;
        const bool passed = sSuite.failed == 0;
        sSmoke.stage = passed ? SmokeStage::Passed : SmokeStage::Failed;
        sSmoke.detail = fmt::format("Suite complete: {} passed, {} failed", sSuite.passed, sSuite.failed);
        WriteSuiteSummary();
        FinishSuiteSession(passed);
        return true;
    }

    const size_t index = sSuite.nextScenario++;
    std::string error;
    if (!BeginScenario(scenarios[index], true, error)) {
        sSuite.failed++;
        sSuite.failedScenarioIds.push_back(scenarios[index].id);
        sSmoke.stage = SmokeStage::Failed;
        sSmoke.detail = error;
        sSuite.advanceDelay = 1;
        return true;
    }
    sSmoke.detail = fmt::format("Suite {}/{}: {}", index + 1, scenarios.size(), scenarios[index].id);
    return true;
}

void UpdateSmoke() {
    if (sAutoRunRequested && !sAutoRunStarted && gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr) {
        sAutoRunStarted = true;
        std::string error;
        if (!StartSmokeSuite(error)) {
            SPDLOG_ERROR("[JPAssist Test Lab] Could not start automated suite: {}", error);
            Ship::Context::GetRawInstance()->GetLogger()->flush();
            std::_Exit(2);
        }
    }
    if (AdvanceSmokeSuite()) {
        return;
    }
    if (sSmoke.stage != SmokeStage::WaitingForScene && sSmoke.stage != SmokeStage::WaitingForMessage) {
        return;
    }
    const TestScenario* scenario = &sPendingScenario;
    if (!sSceneInitializedAfterWarp || gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        if (--sSmoke.framesRemaining <= 0) {
            FinishScenario(*scenario, false, "Timed out waiting for an active scene/player");
        }
        return;
    }

    if (sSmoke.stage == SmokeStage::WaitingForScene) {
        // Give actors and the message context a few frames to settle after OnSceneInit.
        if (sPostSceneDelay-- > 0) {
            return;
        }
        MessageDebug_StartTextBox("", scenario->textId, scenario->language);
        sSmoke.stage = SmokeStage::WaitingForMessage;
        sSmoke.framesRemaining = 180;
        sSmoke.detail = "Message requested; waiting for JP Assist observation";
        return;
    }

    std::string detail;
    if (JPAssist_GetRuntimeStatus().textId == scenario->textId) {
        if (!sSmoke.validate) {
            sSmoke.stage = SmokeStage::Loaded;
            sSmoke.detail = fmt::format("Scenario ready: text {:#06x}", scenario->textId);
            return;
        }
        const bool passed = ValidateScenario(*scenario, detail);
        FinishScenario(*scenario, passed, detail);
    } else if (--sSmoke.framesRemaining <= 0) {
        FinishScenario(*scenario, false, "Timed out waiting for JP Assist to observe the requested message");
    }
}

std::optional<int64_t> ParseCommandInteger(const std::string& value) {
    try {
        return std::stoll(value, nullptr, 0);
    } catch (...) {
        return std::nullopt;
    }
}

const char* StageLabel() {
    switch (sSmoke.stage) {
        case SmokeStage::WaitingForScene:
            return "RUNNING: scene";
        case SmokeStage::WaitingForMessage:
            return "RUNNING: message";
        case SmokeStage::Passed:
            return "PASS";
        case SmokeStage::Loaded:
            return "READY";
        case SmokeStage::Failed:
            return "FAIL";
        default:
            return "IDLE";
    }
}

class TestLabWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
        ImGui::TextUnformatted("JP Assist Test Lab");
        ImGui::TextColored(IsTemporarySession() ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
                                                 : ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                           "%s", IsTemporarySession() ? "Temporary debug session active"
                                                       : "Normal save: progression changes locked");
        if (!IsTemporarySession() && ImGui::Button("Start temporary debug session")) {
            std::string error;
            sSmoke.detail = StartTemporarySession(error) ? "Temporary session ready" : error;
        }

        ImGui::Combo("Progression profile", &sSelectedProfile, kProgressionProfiles,
                     IM_ARRAYSIZE(kProgressionProfiles));
        if (ImGui::Button("Reset/apply progression profile")) {
            std::string error;
            sSmoke.detail = ApplyProgressionProfile(kProgressionProfiles[sSelectedProfile], error)
                                ? fmt::format("Applied {}", kProgressionProfiles[sSelectedProfile])
                                : error;
        }

        ImGui::SeparatorText("Scenario");
        const auto& scenarios = TestScenario_GetAll();
        if (scenarios.empty()) {
            ImGui::TextWrapped("No scenarios loaded: %s", TestScenario_GetLoadError().c_str());
        } else {
            sSelectedScenario = std::clamp(sSelectedScenario, 0, static_cast<int>(scenarios.size()) - 1);
            if (ImGui::BeginCombo("Scenario", scenarios[sSelectedScenario].label.c_str())) {
                for (size_t index = 0; index < scenarios.size(); ++index) {
                    if (ImGui::Selectable(scenarios[index].label.c_str(), sSelectedScenario == static_cast<int>(index))) {
                        sSelectedScenario = static_cast<int>(index);
                    }
                }
                ImGui::EndCombo();
            }
            const TestScenario& scenario = scenarios[sSelectedScenario];
            ImGui::TextWrapped("%s", scenario.description.c_str());
            if (ImGui::Button("Load scenario")) {
                std::string error;
                if (!BeginScenario(scenario, false, error)) {
                    sSmoke.detail = error;
                    sSmoke.stage = SmokeStage::Failed;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Run smoke check")) {
                std::string error;
                if (!BeginScenario(scenario, true, error)) {
                    sSmoke.detail = error;
                    sSmoke.stage = SmokeStage::Failed;
                }
            }
            ImGui::SameLine();
            // Keep BeginDisabled/EndDisabled balanced even when clicking the button
            // starts the suite and changes sSuite.active during this same frame.
            const bool suiteWasActive = sSuite.active;
            if (suiteWasActive) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Run all smoke checks")) {
                std::string error;
                if (!StartSmokeSuite(error)) {
                    sSmoke.detail = error;
                    sSmoke.stage = SmokeStage::Failed;
                }
            }
            if (suiteWasActive) {
                ImGui::EndDisabled();
            }
        }

        ImGui::SeparatorText("Arbitrary entrance/message");
        ImGui::InputScalar("Entrance", ImGuiDataType_S32, &sRawEntrance, nullptr, nullptr, "0x%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::InputScalar("Text ID", ImGuiDataType_S32, &sRawTextId, nullptr, nullptr, "0x%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::RadioButton("Japanese", &sRawLanguage, LANGUAGE_JPN);
        ImGui::SameLine();
        ImGui::RadioButton("English", &sRawLanguage, LANGUAGE_ENG);
        if (ImGui::Button("Warp and display")) {
            TestScenario raw;
            raw.id = "raw";
            raw.label = "Raw entrance/message";
            raw.entrance = sRawEntrance;
            raw.textId = static_cast<uint16_t>(sRawTextId);
            raw.language = static_cast<uint8_t>(sRawLanguage);
            raw.progressionProfile = kProgressionProfiles[sSelectedProfile];
            raw.age = sSelectedProfile >= 2 ? "adult" : "child";
            std::string error;
            if (!BeginScenario(raw, false, error)) {
                sSmoke.detail = error;
                sSmoke.stage = SmokeStage::Failed;
            }
        }

        ImGui::SeparatorText("Live status");
        const RuntimeStatus runtime = JPAssist_GetRuntimeStatus();
        ImGui::Text("Smoke: %s", StageLabel());
        ImGui::TextWrapped("%s", sSmoke.detail.c_str());
        if (sSuite.active) {
            ImGui::Text("Suite: %zu/%zu complete  (%d pass, %d fail)", sSuite.nextScenario - 1,
                        TestScenario_GetAll().size(), sSuite.passed, sSuite.failed);
        }
        ImGui::Text("Scene %s  entrance %#06x", gPlayState ? fmt::format("{:#04x}", gPlayState->sceneNum).c_str() : "--",
                    gSaveContext.entranceIndex);
        ImGui::Text("Text %#06x  page %d  tokens %d", runtime.textId, runtime.pageIndex,
                    runtime.currentPageTokenCount);
        ImGui::Text("Language %s  Study %s  token %d", runtime.requestedLanguage == LANGUAGE_JPN ? "JP" : "EN",
                    runtime.studyModeActive ? "active" : "closed", runtime.selectedTokenIndex);
        ImGui::Text("Choice page %s  choice %u  selection %s", runtime.currentPageIsChoice ? "yes" : "no",
                    runtime.choiceIndex, runtime.choiceSelectionFrozen ? "frozen" : "native");
        ImGui::Text("Observed controls: language %llu, Study %llu, navigation %llu, saves %llu",
                    static_cast<unsigned long long>(runtime.languageToggleCount),
                    static_cast<unsigned long long>(runtime.studyEnterCount),
                    static_cast<unsigned long long>(runtime.studyNavigationCount),
                    static_cast<unsigned long long>(runtime.saveToggleCount));
        ImGui::TextDisabled("Automated smoke verifies warp -> message -> corpus. L/Z, R, layout, and glyph appearance remain manual.");
    }
};

int32_t ScenarioCommand(std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
    if (args.empty()) {
        *output = "Usage: jpassist_scenario <id>";
        return 1;
    }
    const TestScenario* scenario = TestScenario_Find(args[0]);
    if (scenario == nullptr) {
        *output = "Unknown scenario: " + args[0];
        return 1;
    }
    std::string error;
    if (!BeginScenario(*scenario, false, error)) {
        *output = error;
        return 1;
    }
    *output = "Loading scenario " + scenario->id;
    return 0;
}

int32_t SmokeCommand(std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
    if (args.empty()) {
        *output = "Usage: jpassist_smoke <scenario-id>";
        return 1;
    }
    const TestScenario* scenario = TestScenario_Find(args[0]);
    if (scenario == nullptr) {
        *output = "Unknown scenario: " + args[0];
        return 1;
    }
    std::string error;
    if (!BeginScenario(*scenario, true, error)) {
        *output = error;
        return 1;
    }
    *output = "Smoke check started for " + scenario->id;
    return 0;
}

int32_t SmokeAllCommand(std::shared_ptr<Ship::Console>, std::vector<std::string>, std::string* output) {
    std::string error;
    if (!StartSmokeSuite(error)) {
        *output = error;
        return 1;
    }
    *output = fmt::format("Started all {} JP Assist smoke scenarios", TestScenario_GetAll().size());
    return 0;
}

int32_t SessionCommand(std::shared_ptr<Ship::Console>, std::vector<std::string>, std::string* output) {
    std::string error;
    if (!StartTemporarySession(error)) {
        *output = error;
        return 1;
    }
    *output = "Temporary JP Assist test session started; save writes are disabled for file 0xFF.";
    return 0;
}

int32_t ProgressCommand(std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
    if (args.empty()) {
        *output = "Usage: jpassist_progress <debug_child|post_deku_tree|adult_all_access|endgame>";
        return 1;
    }
    std::string error;
    if (!ApplyProgressionProfile(args[0], error)) {
        *output = error;
        return 1;
    }
    *output = "Applied temporary progression profile " + args[0];
    return 0;
}

int32_t WarpCommand(std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
    if (args.empty()) {
        *output = "Usage: jpassist_warp <entrance-id> [progression-profile]";
        return 1;
    }
    const auto entrance = ParseCommandInteger(args[0]);
    if (!entrance || *entrance < 0 || *entrance > INT32_MAX) {
        *output = "Entrance must be a decimal or 0x-prefixed non-negative integer.";
        return 1;
    }
    std::string error;
    if (!IsTemporarySession() && !StartTemporarySession(error)) {
        *output = error;
        return 1;
    }
    const std::string profile = args.size() > 1 ? args[1] : "debug_child";
    TestScenario scenario;
    scenario.entrance = static_cast<int32_t>(*entrance);
    scenario.progressionProfile = profile;
    scenario.age = profile == "adult_all_access" || profile == "endgame" ? "adult" : "child";
    if (!ApplyProgressionProfile(profile, error) || !WarpTo(scenario, error)) {
        *output = error;
        return 1;
    }
    sSmoke.stage = SmokeStage::Idle;
    *output = fmt::format("Warping to entrance {:#06x} with profile {}", scenario.entrance, profile);
    return 0;
}

int32_t MessageCommand(std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
    if (args.empty()) {
        *output = "Usage: jpassist_message <text-id> [jpn|eng]";
        return 1;
    }
    const auto textId = ParseCommandInteger(args[0]);
    if (!textId || *textId < 0 || *textId > UINT16_MAX) {
        *output = "Text ID must be a decimal or 0x-prefixed 16-bit integer.";
        return 1;
    }
    if (gPlayState == nullptr || GET_PLAYER(gPlayState) == nullptr) {
        *output = "Load a scene before opening a message.";
        return 1;
    }
    const uint8_t language = args.size() > 1 && (args[1] == "eng" || args[1] == "english") ? LANGUAGE_ENG
                                                                                              : LANGUAGE_JPN;
    MessageDebug_StartTextBox("", static_cast<uint16_t>(*textId), language);
    *output = fmt::format("Opened text {:#06x} in {}", *textId, language == LANGUAGE_JPN ? "Japanese" : "English");
    return 0;
}

int32_t StatusCommand(std::shared_ptr<Ship::Console>, std::vector<std::string>, std::string* output) {
    const RuntimeStatus status = JPAssist_GetRuntimeStatus();
    *output = fmt::format("{}: {}; text={:#06x} page={} tokens={} language={} study={} temporary={}", StageLabel(),
                          sSmoke.detail, status.textId, status.pageIndex, status.currentPageTokenCount,
                          status.requestedLanguage == LANGUAGE_JPN ? "JP" : "EN", status.studyModeActive,
                          IsTemporarySession());
    return 0;
}

} // namespace

void JPAssistTestLab_Register() {
    TestScenario_LoadManifest();
    const char* autoRun = std::getenv("JPASSIST_AUTORUN_SMOKE");
    sAutoRunRequested = autoRun != nullptr && std::string(autoRun) != "0";
    if (sAutoRunRequested) {
        SPDLOG_INFO("[JPAssist Test Lab] Automated smoke suite requested; waiting for an active PlayState");
    }
    sWindow = std::make_shared<TestLabWindow>(CVAR_WINDOW("JPAssistTestLab"), "JP Assist Test Lab", ImVec2(620, 680));
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(sWindow);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(UpdateSmoke);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        if (sSmoke.stage == SmokeStage::WaitingForScene) {
            sSceneInitializedAfterWarp = true;
            sPostSceneDelay = 15;
        }
    });
    auto console = Ship::Context::GetRawInstance()->GetConsole();
    console->AddCommand("jpassist_test_session", { SessionCommand, "Start a non-persistent JP Assist test session." });
    console->AddCommand("jpassist_progress", { ProgressCommand, "Apply a temporary progression profile." });
    console->AddCommand("jpassist_warp", { WarpCommand, "Warp to an arbitrary entrance ID." });
    console->AddCommand("jpassist_message", { MessageCommand, "Open an arbitrary Japanese or English message." });
    console->AddCommand("jpassist_scenario", { ScenarioCommand, "Load a JP Assist Test Lab scenario." });
    console->AddCommand("jpassist_smoke", { SmokeCommand, "Run a JP Assist smoke scenario." });
    console->AddCommand("jpassist_smoke_all", { SmokeAllCommand, "Run all JP Assist smoke scenarios." });
    console->AddCommand("jpassist_status", { StatusCommand, "Show JP Assist Test Lab/runtime status." });
}

} // namespace JPAssist
