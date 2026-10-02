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

enum class SmokeControlStep {
    None,
    PressStudy,
    AwaitStudy,
    PressNavigate,
    AwaitNavigate,
    PressSave,
    AwaitSave,
    PressRestoreSave,
    AwaitRestoreSave,
    PressScrollDown,
    AwaitScrollDown,
    PressScrollUp,
    AwaitScrollUp,
    PressAdvance,
    AwaitAdvance,
    WaitForChoice,
    PressChoiceDeflection,
    AwaitChoiceDeflection,
    PressExit,
    AwaitExit,
};

struct SmokeState {
    SmokeStage stage = SmokeStage::Idle;
    std::string scenarioId;
    std::string detail = "Not run";
    int framesRemaining = 0;
    uint64_t startedAtStudyCount = 0;
    uint64_t startedAtNavigationCount = 0;
    uint64_t expectedSaveToggleCount = 0;
    uint64_t expectedScrollCount = 0;
    uint8_t frozenChoiceIndex = 0;
    int initialPageIndex = 0;
    int advancePressCount = 0;
    bool initiallySaved = false;
    SmokeControlStep controlStep = SmokeControlStep::None;
    std::string corpusDetail;
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

void InjectButton(uint16_t button) {
    JPAssist_QueueTestInput(button);
}

// This hook is registered before JPAssistManager's OnDialogMessage hook, so
// synthetic presses enter the same shared Input object immediately before the
// production handler reads and consumes them. OnGameFrameUpdate is too late:
// it runs at the end of the frame, after Message_Update.
void InjectSmokeControl() {
    if (!sSmoke.validate || gPlayState == nullptr || sSmoke.stage != SmokeStage::WaitingForMessage ||
        !gPlayState->state.running || gPlayState->msgCtx.textId != sPendingScenario.textId) {
        return;
    }

    switch (sSmoke.controlStep) {
        case SmokeControlStep::PressStudy:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting Study Mode R");
            InjectButton(BTN_R);
            sSmoke.controlStep = SmokeControlStep::AwaitStudy;
            sSmoke.framesRemaining = 30;
            break;
        case SmokeControlStep::PressNavigate:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting Study navigation D-Right");
            InjectButton(BTN_DRIGHT);
            sSmoke.controlStep = SmokeControlStep::AwaitNavigate;
            sSmoke.framesRemaining = 30;
            break;
        case SmokeControlStep::PressSave:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting save toggle C-Right");
            InjectButton(BTN_CRIGHT);
            sSmoke.expectedSaveToggleCount++;
            sSmoke.controlStep = SmokeControlStep::AwaitSave;
            sSmoke.framesRemaining = 30;
            break;
        case SmokeControlStep::PressRestoreSave:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting restore save toggle C-Right");
            InjectButton(BTN_CRIGHT);
            sSmoke.expectedSaveToggleCount++;
            sSmoke.controlStep = SmokeControlStep::AwaitRestoreSave;
            sSmoke.framesRemaining = 30;
            break;
        case SmokeControlStep::PressScrollDown:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting Study scroll D-Down");
            InjectButton(BTN_DDOWN);
            sSmoke.expectedScrollCount++;
            sSmoke.controlStep = SmokeControlStep::AwaitScrollDown;
            sSmoke.framesRemaining = 30;
            break;
        case SmokeControlStep::PressScrollUp:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting Study scroll D-Up");
            InjectButton(BTN_DUP);
            sSmoke.expectedScrollCount++;
            sSmoke.controlStep = SmokeControlStep::AwaitScrollUp;
            sSmoke.framesRemaining = 30;
            break;
        case SmokeControlStep::PressAdvance:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting native dialogue advance A (attempt {})",
                        sSmoke.advancePressCount + 1);
            InjectButton(BTN_A);
            sSmoke.advancePressCount++;
            sSmoke.controlStep = SmokeControlStep::AwaitAdvance;
            break;
        case SmokeControlStep::PressChoiceDeflection: {
            SPDLOG_INFO("[JPAssist Test Lab] Injecting choice deflection (analog down + D-down)");
            sSmoke.frozenChoiceIndex = gPlayState->msgCtx.choiceIndex;
            JPAssist_QueueTestInput(BTN_DDOWN, -80, true);
            sSmoke.controlStep = SmokeControlStep::AwaitChoiceDeflection;
            sSmoke.framesRemaining = 30;
            break;
        }
        case SmokeControlStep::PressExit:
            SPDLOG_INFO("[JPAssist Test Lab] Injecting Study exit B");
            InjectButton(BTN_B);
            sSmoke.controlStep = SmokeControlStep::AwaitExit;
            sSmoke.framesRemaining = 30;
            break;
        default:
            break;
    }
}

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
    // cannot inherit equipment or event flags from the previous case.
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

    const bool adult = scenario.age == "adult";
    gSaveContext.linkAge = adult ? LINK_AGE_ADULT : LINK_AGE_CHILD;
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

void FinishScenario(const TestScenario& scenario, bool passed, const std::string& detail);

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

void StartControlValidation(const std::string& corpusDetail) {
    const RuntimeStatus runtime = JPAssist_GetRuntimeStatus();
    sSmoke.corpusDetail = corpusDetail;
    sSmoke.startedAtStudyCount = runtime.studyEnterCount;
    sSmoke.startedAtNavigationCount = runtime.studyNavigationCount;
    sSmoke.initialPageIndex = runtime.pageIndex;
    sSmoke.advancePressCount = 0;
    sSmoke.controlStep = SmokeControlStep::PressStudy;
    sSmoke.framesRemaining = 30;
    sSmoke.detail = "Corpus passed; entering Study Mode";
    SPDLOG_INFO("[JPAssist Test Lab] Starting R-button Study Mode smoke for {}", sPendingScenario.id);
}

bool ControlTimedOut(const TestScenario& scenario, const std::string& expectation) {
    if (--sSmoke.framesRemaining > 0) {
        return false;
    }
    FinishScenario(scenario, false, "Control smoke timed out: " + expectation);
    return true;
}

bool ShouldValidateNativeAdvance(const TestScenario& scenario) {
    const StudyPage* firstPage = StudyRepository_FindPage(scenario.textId, 0);
    const StudyPage* secondPage = StudyRepository_FindPage(scenario.textId, 1);
    return firstPage != nullptr && secondPage != nullptr && firstPage->japanese != secondPage->japanese;
}

void FinishControlValidation(const TestScenario& scenario) {
    FinishScenario(scenario, true,
                   sSmoke.corpusDetail +
                       "; controls PASS: R Study enter/exit, save/restore, D-pad scroll, focus consumption" +
                       (StudyRepository_FindPage(scenario.textId, 0)->tokens.size() > 1 ? ", token navigation" : "") +
                       (ShouldValidateNativeAdvance(scenario) ? ", native advancement while studying" : "") +
                       (StudyRepository_FindPage(scenario.textId, 0)->isChoice ? ", choice freeze" : ""));
}

void StartStudyInteractionValidation(const RuntimeStatus& runtime, const std::string& completedStep) {
    if (runtime.currentPageTokenCount <= 0) {
        if (runtime.currentPageIsChoice) {
            sSmoke.controlStep = SmokeControlStep::WaitForChoice;
            sSmoke.framesRemaining = 360;
        } else {
            sSmoke.controlStep = SmokeControlStep::PressExit;
        }
        return;
    }

    sSmoke.expectedSaveToggleCount = runtime.saveToggleCount;
    sSmoke.expectedScrollCount = runtime.studyScrollCount;
    sSmoke.initiallySaved = runtime.selectedTokenSaved;
    sSmoke.controlStep = SmokeControlStep::PressSave;
    sSmoke.detail = completedStep + "; testing save toggle";
}

void UpdateControlValidation(const TestScenario& scenario) {
    const RuntimeStatus runtime = JPAssist_GetRuntimeStatus();
    switch (sSmoke.controlStep) {
        case SmokeControlStep::AwaitStudy:
            if (runtime.studyModeActive && runtime.studyEnterCount > sSmoke.startedAtStudyCount) {
                if (runtime.currentPageTokenCount > 1) {
                    sSmoke.controlStep = SmokeControlStep::PressNavigate;
                    sSmoke.detail = "Study entry passed; testing token navigation";
                } else {
                    StartStudyInteractionValidation(runtime, "Study entry passed");
                }
            } else {
                ControlTimedOut(scenario, "R did not enter Study Mode");
            }
            break;
        case SmokeControlStep::AwaitNavigate:
            if (runtime.selectedTokenIndex == 1 &&
                runtime.studyNavigationCount > sSmoke.startedAtNavigationCount) {
                StartStudyInteractionValidation(runtime, "Navigation passed");
            } else {
                ControlTimedOut(scenario, "D-Right did not select the next token");
            }
            break;
        case SmokeControlStep::AwaitSave:
            if (runtime.saveToggleCount >= sSmoke.expectedSaveToggleCount &&
                runtime.selectedTokenSaved != sSmoke.initiallySaved) {
                sSmoke.controlStep = SmokeControlStep::PressRestoreSave;
                sSmoke.detail = "Save toggle passed; restoring initial study-list state";
            } else {
                ControlTimedOut(scenario, "C-Right did not toggle the selected token's saved state");
            }
            break;
        case SmokeControlStep::AwaitRestoreSave:
            if (runtime.saveToggleCount >= sSmoke.expectedSaveToggleCount &&
                runtime.selectedTokenSaved == sSmoke.initiallySaved) {
                sSmoke.controlStep = SmokeControlStep::PressScrollDown;
                sSmoke.detail = "Save restore passed; testing D-Down scroll";
            } else {
                ControlTimedOut(scenario, "second C-Right did not restore the initial saved state");
            }
            break;
        case SmokeControlStep::AwaitScrollDown:
            if (runtime.studyScrollCount >= sSmoke.expectedScrollCount &&
                !CHECK_BTN_ALL(gPlayState->state.input[0].press.button, BTN_DDOWN)) {
                sSmoke.controlStep = SmokeControlStep::PressScrollUp;
                sSmoke.detail = "D-Down scroll passed; testing D-Up scroll";
            } else {
                ControlTimedOut(scenario, "D-Down did not scroll or leaked through Study focus");
            }
            break;
        case SmokeControlStep::AwaitScrollUp:
            if (runtime.studyScrollCount >= sSmoke.expectedScrollCount &&
                !CHECK_BTN_ALL(gPlayState->state.input[0].press.button, BTN_DUP)) {
                if (runtime.currentPageIsChoice) {
                    sSmoke.controlStep = SmokeControlStep::WaitForChoice;
                    sSmoke.framesRemaining = 360;
                    sSmoke.detail = "Study interactions passed; waiting for native choice state";
                } else if (ShouldValidateNativeAdvance(scenario)) {
                    sSmoke.controlStep = SmokeControlStep::PressAdvance;
                    sSmoke.framesRemaining = 60;
                    sSmoke.detail = "Study interactions passed; testing native A advancement";
                } else {
                    sSmoke.controlStep = SmokeControlStep::PressExit;
                    sSmoke.detail = "Study interactions passed; exiting Study Mode";
                }
            } else {
                ControlTimedOut(scenario, "D-Up did not scroll or leaked through Study focus");
            }
            break;
        case SmokeControlStep::AwaitAdvance:
            if (runtime.pageIndex > sSmoke.initialPageIndex) {
                if (runtime.studyModeActive && runtime.selectedTokenIndex == 0) {
                    sSmoke.controlStep = SmokeControlStep::PressExit;
                    sSmoke.detail = "Native A advancement passed; Study Mode followed the new page";
                } else {
                    FinishScenario(scenario, false,
                                   "Native page advanced, but Study Mode closed or token selection did not reset");
                }
            } else if (sSmoke.advancePressCount < 6 && sSmoke.framesRemaining < 60 &&
                       sSmoke.framesRemaining % 10 == 0) {
                // A does not skip every native typewriter state. Retry at a
                // human-scale cadence until the page reaches AWAIT_NEXT;
                // the strict cap and timeout still catch swallowed input.
                sSmoke.framesRemaining--;
                sSmoke.controlStep = SmokeControlStep::PressAdvance;
            } else {
                ControlTimedOut(scenario, "A did not advance the native page while Study Mode remained open");
            }
            break;
        case SmokeControlStep::WaitForChoice:
            if (gPlayState->msgCtx.msgMode == MSGMODE_TEXT_DONE && runtime.choiceSelectionFrozen) {
                sSmoke.controlStep = SmokeControlStep::PressChoiceDeflection;
                sSmoke.detail = "Choice ready; testing analog/D-pad focus consumption";
            } else {
                ControlTimedOut(scenario, "choice page did not reach a frozen TEXT_DONE state");
            }
            break;
        case SmokeControlStep::AwaitChoiceDeflection:
            if (runtime.choiceSelectionFrozen && runtime.choiceIndex == sSmoke.frozenChoiceIndex &&
                gPlayState->state.input[0].rel.stick_y == 0 &&
                !CHECK_BTN_ALL(gPlayState->state.input[0].press.button, BTN_DDOWN)) {
                sSmoke.controlStep = SmokeControlStep::PressExit;
                sSmoke.detail = "Choice focus passed; exiting Study Mode";
            } else {
                ControlTimedOut(scenario, "choice moved or vertical input leaked through Study focus");
            }
            break;
        case SmokeControlStep::AwaitExit:
            if (!runtime.studyModeActive) {
                sSmoke.controlStep = SmokeControlStep::None;
                FinishControlValidation(scenario);
            } else {
                ControlTimedOut(scenario, "B did not exit Study Mode");
            }
            break;
        default:
            break;
    }
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
    sSmoke.startedAtStudyCount = runtime.studyEnterCount;
    sSmoke.startedAtNavigationCount = runtime.studyNavigationCount;
    sSmoke.controlStep = SmokeControlStep::None;
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
        if (sSuite.nextScenario >= TestScenario_GetAll().size()) {
            sSuite.active = false;
            const bool suitePassed = sSuite.failed == 0;
            sSmoke.stage = suitePassed ? SmokeStage::Passed : SmokeStage::Failed;
            sSmoke.detail = fmt::format("Suite complete: {} passed, {} failed", sSuite.passed, sSuite.failed);
            WriteSuiteSummary();
            FinishSuiteSession(suitePassed);
            return;
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
    if (!sSceneInitializedAfterWarp || gPlayState == nullptr || !gPlayState->state.running ||
        GET_PLAYER(gPlayState) == nullptr) {
        if (--sSmoke.framesRemaining <= 0) {
            FinishScenario(*scenario, false, "Timed out waiting for an active scene/player");
        }
        return;
    }

    if (sSmoke.controlStep != SmokeControlStep::None) {
        UpdateControlValidation(*scenario);
        return;
    }

    if (sSmoke.stage == SmokeStage::WaitingForScene) {
        // Give actors and the message context a few frames to settle after the
        // incoming transition has fully completed.
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
        if (!ValidateScenario(*scenario, detail)) {
            FinishScenario(*scenario, false, detail);
        } else if (!scenario->validateControls) {
            FinishScenario(*scenario, true, detail + "; persistence-only scenario (controls skipped)");
        } else {
            StartControlValidation(detail);
        }
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

const char* DisplayModeLabel(DialogueStudy::DialogueDisplayMode mode) {
    switch (mode) {
        case DialogueStudy::DialogueDisplayMode::NativeSwap:
            return "native swap";
        case DialogueStudy::DialogueDisplayMode::JapaneseOnly:
            return "Japanese only";
        case DialogueStudy::DialogueDisplayMode::AttachedTranslation:
        default:
            return "attached translation";
    }
}

const char* DialogueSurfaceLabel(DialogueStudy::DialogueSurface surface) {
    switch (surface) {
        case DialogueStudy::DialogueSurface::NativeTextbox:
            return "native textbox";
        case DialogueStudy::DialogueSurface::AttachedPanel:
            return "attached panel";
        case DialogueStudy::DialogueSurface::Hidden:
        default:
            return "hidden";
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
        ImGui::Text("Study %s  token %d", runtime.studyModeActive ? "active" : "closed",
                    runtime.selectedTokenIndex);
        ImGui::Text("Display %s -> %s%s", DisplayModeLabel(runtime.displayMode),
                    DialogueSurfaceLabel(runtime.dialogueSurface), runtime.displayModeFallback ? " (fallback)" : "");
        ImGui::Text("Choice page %s  choice %u  selection %s", runtime.currentPageIsChoice ? "yes" : "no",
                    runtime.choiceIndex, runtime.choiceSelectionFrozen ? "frozen" : "native");
        ImGui::Text("Observed controls: Study %llu, navigation %llu, saves %llu",
                    static_cast<unsigned long long>(runtime.studyEnterCount),
                    static_cast<unsigned long long>(runtime.studyNavigationCount),
                    static_cast<unsigned long long>(runtime.saveToggleCount));
        ImGui::TextDisabled("Smoke verifies warp, corpus, R, navigation, focus consumption, and choice freeze.");
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
    *output = fmt::format("{}: {}; text={:#06x} page={} tokens={} language={} display={} surface={}{} study={} temporary={}", StageLabel(),
                          sSmoke.detail, status.textId, status.pageIndex, status.currentPageTokenCount,
                          status.requestedLanguage == LANGUAGE_JPN ? "JP" : "EN", DisplayModeLabel(status.displayMode),
                          DialogueSurfaceLabel(status.dialogueSurface), status.displayModeFallback ? "(fallback)" : "",
                          status.studyModeActive, IsTemporarySession());
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
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDialogMessage>(InjectSmokeControl);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(UpdateSmoke);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnTransitionEnd>([](int16_t) {
        if (sSmoke.stage == SmokeStage::WaitingForScene) {
            sSceneInitializedAfterWarp = true;
            sPostSceneDelay = 3;
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
