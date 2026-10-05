#include "JPAssistManager.h"

#include <algorithm>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "DialogueRepository.h"
#include "JPAssistHistory.h"
#include "ShipwrightJPAssistHost.h"
#include "JPAssistTestLab.h"
#include "MessageParser.h"
#include "StudyPersistence.h"
#include "StudyRepository.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/ModApi/StudyModApiInternal.h"
#include "variables.h"
#include "z64.h"

#include <ship/Context.h>
#include <ship/window/Window.h>

#include <libultraship/bridge/consolevariablebridge.h>
#include <soh/SohGui/SohMenu.h>
#include <soh/SohGui/SohGui.hpp>
#include <soh/SohGui/UIWidgetOptions.hpp>
#include <soh/SohGui/UIWidgets.hpp>
#include <soh/cvar_prefixes.h>

#include <ship/debug/Console.h>

extern PlayState* gPlayState;
// Same pattern soh/soh/Enhancements/Presets/Presets.cpp:46-48 and several
// other independent enhancement files use to add their own sidebar/widgets
// without needing to touch soh/soh/SohGui/SohMenuEnhancements.cpp: mSohMenu
// is constructed once during Gui setup (soh/soh/SohGui/SohGui.cpp:104)
// before any RegisterShipInitFunc-registered function (including
// RegisterJPAssist, below) runs. The extern must be lexically inside
// namespace SohGui, not just qualified with SohGui:: - otherwise it
// declares an unrelated ::mSohMenu and the linker can't find the real
// symbol.
namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace {

uint16_t sLastPluginHistoryTextId = 0xFFFF;

bool sRomCompatibilityChecked = false;

bool PluginRuntimeEnabled() {
    // Successful callback registration is the ownership boundary. Diagnostics
    // describe the plugin state for UI/tests, but must not reactivate a second
    // source-integrated runtime if a diagnostic CVar is stale or reset.
    return StudyModApi::HasRegisteredMod("jp-assist");
}

void MirrorPluginSettings() {
    CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.Enabled"),
                   CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.Enabled"), 1));
    CVarSetFloat(CVAR_ENHANCEMENT("StudyMods.jp-assist.CardScale"),
                 CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardScale"), 1.0f));
    CVarSetFloat(CVAR_ENHANCEMENT("StudyMods.jp-assist.CardOpacity"),
                 CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardOpacity"), 0.92f));
    CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountSyncEnabled"),
                   CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.AccountSync.Enabled"), 0));
    CVarSetString(CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountSyncEndpoint"),
                  CVarGetString(CVAR_ENHANCEMENT("JPAssist.AccountSync.Endpoint"), "http://127.0.0.1:8766"));
}

void MigrateLegacyPluginState() {
    const std::filesystem::path destinationRoot =
        Ship::Context::GetPathRelativeToAppDirectory("mods/jp-assist");
    std::error_code error;
    bool changed = false;
    std::filesystem::create_directories(destinationRoot, error);
    for (const char* fileName : { "jp_assist_progress.json", "jp_assist_sync.json" }) {
        const std::filesystem::path source = Ship::Context::GetPathRelativeToAppDirectory(fileName);
        const std::filesystem::path destination = destinationRoot / fileName;
        if (!std::filesystem::exists(source) || std::filesystem::exists(destination)) {
            continue;
        }
        error.clear();
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
        if (error) {
            SPDLOG_WARN("[JPAssist] Could not migrate {} to plugin storage: {}", fileName, error.message());
        } else {
            SPDLOG_INFO("[JPAssist] Migrated {} to plugin storage", fileName);
            changed = true;
        }
    }

    for (const char* profile : { "NoDialogue", "UpperDialogue", "LowerDialogue" }) {
        const std::string sourcePrefix = std::string(CVAR_ENHANCEMENT("JPAssist.Layout.")) + profile + ".";
        const std::string destinationPrefix =
            std::string(CVAR_ENHANCEMENT("StudyMods.jp-assist.Layout.")) + profile + ".";
        if (CVarGetInteger((sourcePrefix + "Valid").c_str(), 0) == 0 ||
            CVarGetInteger((destinationPrefix + "Valid").c_str(), 0) != 0) {
            continue;
        }
        CVarSetFloat((destinationPrefix + "X").c_str(), CVarGetFloat((sourcePrefix + "X").c_str(), 0.0f));
        CVarSetFloat((destinationPrefix + "Y").c_str(), CVarGetFloat((sourcePrefix + "Y").c_str(), 0.0f));
        CVarSetFloat((destinationPrefix + "Width").c_str(),
                     CVarGetFloat((sourcePrefix + "Width").c_str(), 0.0f));
        CVarSetFloat((destinationPrefix + "Height").c_str(),
                     CVarGetFloat((sourcePrefix + "Height").c_str(), 0.0f));
        CVarSetInteger((destinationPrefix + "Valid").c_str(), 1);
        changed = true;
    }

    if (changed) {
        // Code mods initialize before RegisterJPAssist on current Shipwright
        // startup. Ask the already-running plugin to reload any files that
        // were copied after its initial read.
        CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.ReloadState"), 1);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

// Lightweight runtime compatibility signal. The generated corpus also stores
// per-language source hashes for deeper validation once raw-entry hashing is
// exposed by the runtime repository.
void CheckRomCompatibilityOnce() {
    if (sRomCompatibilityChecked) {
        return;
    }
    sRomCompatibilityChecked = true;

    struct KnownMessage {
        uint16_t textId;
        const char* description;
    };
    const KnownMessage knownMessages[] = {
        { 0x1001, "Saria's first greeting" },
        { 0x033C, "Mido's House sign" },
        { 0x103E, "Know-It-All Brothers choice" },
    };

    int missing = 0;
    for (const auto& known : knownMessages) {
        const char* segment = nullptr;
        uint32_t length = 0;
        bool foundJpn = JPAssist::DialogueRepository_Find(known.textId, LANGUAGE_JPN, &segment, &length);
        bool foundEng = JPAssist::DialogueRepository_Find(known.textId, LANGUAGE_ENG, &segment, &length);
        if (!foundJpn || !foundEng) {
            missing++;
            SPDLOG_WARN("[JPAssist] Compatibility check: textId {:#x} ({}) missing from {} - this build was tested "
                        "against N64 NTSC 1.2; a different ROM/oot.o2r may not match JP Assist's recorded test "
                        "dialogues.",
                        known.textId, known.description, (!foundJpn && !foundEng) ? "both language tables"
                                                                                  : (!foundJpn ? "the Japanese table"
                                                                                               : "the English table"));
        }
    }
    if (missing == 0) {
        SPDLOG_INFO("[JPAssist] Compatibility check: all known test dialogues found in both language tables "
                    "(N64 NTSC 1.2 expected).");
    }
}

// Design doc section 9 / section 11's "dialogue history": records page 0's
// English text for whatever textId just opened. Prefer the normalized corpus,
// which retains choice text; use the raw-table parser only as a fallback.
void RecordHistoryForOpenedMessage(uint16_t textId) {
    std::string text;
    if (const JPAssist::StudyPage* page = JPAssist::StudyRepository_FindPage(textId, 0); page != nullptr) {
        // An empty corpus translation is intentional: the build-time
        // aligner found a suspicious JP/EN pair and suppressed it pending
        // review. Falling back to the same raw English textId here would
        // reintroduce the exact incorrect translation the alignment layer
        // is protecting the overlay and history from.
        text = page->english;
    } else {
        const char* segment = nullptr;
        uint32_t length = 0;
        if (!JPAssist::DialogueRepository_Find(textId, LANGUAGE_ENG, &segment, &length)) {
            return;
        }
        JPAssist::DialogueStructure structure = JPAssist::MessageParser_Parse(segment, length, LANGUAGE_ENG);
        if (structure.pages.empty()) {
            return;
        }
        text = structure.pages[0].englishText;
    }
    JPAssist::StudyPersistence_RecordHistoryEntry(textId, text);
    // Saved immediately rather than batched: dialogue opens are already
    // infrequent (nowhere near once-per-frame or once-per-page-flip), so
    // the extra write here doesn't add meaningful I/O pressure, and without
    // it, playing without ever opening Study Mode would leave history
    // accumulating in memory with no other event around to flush it.
    JPAssist::StudyPersistence_Save();
}

void OnDialogMessage() {
    // The .o2r plugin owns study state, input, rendering, native highlighting,
    // persistence, audio, and account sync. This source companion observes
    // message opens only so Shipwright's searchable local history window can
    // remain available without duplicating the plugin runtime.
    if (!PluginRuntimeEnabled() || !CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.Enabled"), 1) ||
        gPlayState == nullptr || gPlayState->msgCtx.msgLength == 0 ||
        gPlayState->msgCtx.msgMode == MSGMODE_NONE || gPlayState->msgCtx.msgMode == MSGMODE_TEXT_CLOSING) {
        sLastPluginHistoryTextId = 0xFFFF;
        return;
    }

    CheckRomCompatibilityOnce();
    if (gPlayState->msgCtx.textId != sLastPluginHistoryTextId) {
        sLastPluginHistoryTextId = gPlayState->msgCtx.textId;
        RecordHistoryForOpenedMessage(sLastPluginHistoryTextId);
    }
}

// Study Mode's bindings aren't rebindable yet; that needs the input-editor
// integration real button remapping uses. Mirrors the pattern
// soh/soh/Enhancements/Presets/Presets.cpp:494 and several other
// independent enhancements use to add their own sidebar without touching
// soh/soh/SohGui/SohMenuEnhancements.cpp.
void RegisterJPAssistMenu() {
    WidgetPath path = { "Enhancements", "JP Assist", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddSidebarEntry("Enhancements", path.sidebarName, 1);

    SohGui::mSohMenu->AddWidget(path, "Enable JP Assist", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("JPAssist.Enabled"))
        .Callback([](WidgetInfo&) { MirrorPluginSettings(); })
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
            "Master toggle for the R/L/Z Study Mode language-learning tools. "
            "Disabling this leaves the game exactly as if the mod weren't installed."));
    SohGui::mSohMenu->AddWidget(path, "JPAssistRuntimeStatus", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo&) {
            ImGui::TextDisabled("Runtime: %s", PluginRuntimeEnabled() ? "jp-assist.o2r plugin" : "plugin not loaded");
        })
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "Study card scale: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("JPAssist.CardScale"))
        .Callback([](WidgetInfo&) { MirrorPluginSettings(); })
        .Options(UIWidgets::FloatSliderOptions().Min(0.70f).Max(1.50f).Step(0.05f).DefaultValue(1.0f).Format("%.2f"));
    SohGui::mSohMenu->AddWidget(path, "Study card opacity: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("JPAssist.CardOpacity"))
        .Callback([](WidgetInfo&) { MirrorPluginSettings(); })
        .Options(UIWidgets::FloatSliderOptions().Min(0.40f).Max(1.0f).Step(0.05f).DefaultValue(0.92f).Format("%.2f"));
    SohGui::mSohMenu->AddWidget(path, "Reset study card layouts", WIDGET_BUTTON)
        .Callback([](WidgetInfo&) {
            CVarClearBlock(CVAR_ENHANCEMENT("JPAssist.Layout."));
            CVarClearBlock(CVAR_ENHANCEMENT("StudyMods.jp-assist.Layout."));
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Forget the mouse-adjusted upper, lower, and no-dialogue card layouts and return to automatic placement."));
    SohGui::mSohMenu->AddWidget(path, "Open JP Assist Test Lab", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("JPAssistTestLab"))
        .WindowName("JP Assist Test Lab")
        .HideInSearch(true)
        .Options(UIWidgets::WindowButtonOptions().Tooltip(
            "Open developer scenarios, temporary progression profiles, live diagnostics, and smoke checks."));
    SohGui::mSohMenu->AddWidget(path, "Open Dialogue History", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("JPAssistHistory"))
        .WindowName("JP Assist Dialogue History")
        .Options(UIWidgets::WindowButtonOptions().Tooltip(
            "Browse and search the 20 most recently opened Japanese and English dialogue lines."));

    SohGui::mSohMenu->AddWidget(path, "Learning account (optional)", WIDGET_SEPARATOR_TEXT);
    SohGui::mSohMenu->AddWidget(path, "Sync learning progress", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("JPAssist.AccountSync.Enabled"))
        .Callback([](WidgetInfo&) { MirrorPluginSettings(); })
        .Options(UIWidgets::CheckboxOptions().DefaultValue(false).Tooltip(
            "Synchronize content-neutral word IDs, encounter counts, and saved state. Dialogue text stays local."));
    SohGui::mSohMenu->AddWidget(path, "Service URL", WIDGET_CUSTOM).CustomFunction([](WidgetInfo& info) {
        ImGui::TextUnformatted(info.name.c_str());
        if (UIWidgets::CVarInputString(
                "##JPAssistAccountEndpoint", CVAR_ENHANCEMENT("JPAssist.AccountSync.Endpoint"),
                UIWidgets::InputOptions()
                    .Color(THEME_COLOR)
                    .PlaceholderText("http://127.0.0.1:8766")
                    .DefaultValue("http://127.0.0.1:8766")
                    .Size(ImVec2(ImGui::GetContentRegionAvail().x, 0))
                    .LabelPosition(UIWidgets::LabelPositions::None))) {
            MirrorPluginSettings();
        }
    });
    SohGui::mSohMenu->AddWidget(path, "Connect learning account", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            const bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.AccountSync.Enabled"), 0) != 0;
            const bool transport = CVarGetInteger(
                                       CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountTransportAvailable"), 0) != 0;
            const bool connected = CVarGetInteger(
                                       CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountConnected"), 0) != 0;
            const bool pairing = CVarGetInteger(
                                     CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPairing"), 0) != 0;
            info.options->disabled = !PluginRuntimeEnabled() || !enabled || !transport || connected || pairing;
        })
        .Callback([](WidgetInfo&) {
            if (PluginRuntimeEnabled()) {
                CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.BeginPairing"), 1);
            }
        })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Request a short-lived code, then approve this game from the learning website. Your password is never "
            "entered into the game."));
    SohGui::mSohMenu->AddWidget(path, "LearningAccountStatus", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo&) {
            if (!PluginRuntimeEnabled()) {
                ImGui::TextDisabled("Install jp-assist.o2r to connect an account.");
                return;
            }
            const char* message = CVarGetString(
                CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountStatusMessage"), "Not connected");
            ImGui::TextWrapped("%s", message[0] == '\0' ? "Not connected" : message);
            if (CVarGetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPairing"), 0) != 0) {
                const char* code = CVarGetString(
                    CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountUserCode"), "");
                const char* address = CVarGetString(
                    CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountVerificationUrl"), "");
                ImGui::Text("Code: %s", code);
                ImGui::TextWrapped("Open: %s", address);
                if (UIWidgets::Button("Copy address and code##JPAssistPluginPairing",
                                      UIWidgets::ButtonOptions().Color(THEME_COLOR))) {
                    const std::string clipboard = std::string(address) + "\n" + code;
                    ImGui::SetClipboardText(clipboard.c_str());
                }
            }
            const int pending = CVarGetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPendingEvents"), 0);
            if (pending != 0) {
                ImGui::Text("Waiting to sync: %d events", pending);
            }
        })
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "Disconnect learning account", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            const bool connected = CVarGetInteger(
                                       CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountConnected"), 0) != 0;
            const bool pairing = CVarGetInteger(
                                     CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPairing"), 0) != 0;
            info.options->disabled = !PluginRuntimeEnabled() || (!connected && !pairing);
        })
        .Callback([](WidgetInfo&) {
            if (PluginRuntimeEnabled()) {
                CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.Disconnect"), 1);
            }
        });
}

// Console companion to the searchable GUI history browser. Keeping this
// command is useful for diagnostics and text-only test sessions.
int32_t JPAssistHistoryCommand(std::shared_ptr<Ship::Console> console, std::vector<std::string> args,
                               std::string* output) {
    const auto& history = JPAssist::StudyPersistence_GetHistory();
    if (history.empty()) {
        if (output != nullptr) {
            *output = "No dialogue history yet.";
        }
        return 0;
    }
    std::string result;
    for (const auto& entry : history) {
        result += fmt::format("[{:#06x}] {}\n", entry.textId, entry.englishText);
    }
    if (output != nullptr) {
        *output = result;
    }
    return 0;
}

void RegisterJPAssist() {
    JPAssist::ShipwrightJPAssistHost_Install();
    JPAssist::StudyRepository_LoadCorpus();
    JPAssist::StudyPersistence_Load();
    MigrateLegacyPluginState();
    MirrorPluginSettings();
    JPAssist::JPAssistHistory_Register();
    JPAssist::JPAssistTestLab_Register();
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDialogMessage>(OnDialogMessage);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (gPlayState == nullptr || gPlayState->msgCtx.msgLength == 0 ||
            gPlayState->msgCtx.msgMode == MSGMODE_NONE || gPlayState->msgCtx.msgMode == MSGMODE_TEXT_CLOSING) {
            sLastPluginHistoryTextId = 0xFFFF;
        }
    });
    RegisterJPAssistMenu();
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "jpassist_history", { JPAssistHistoryCommand, "Lists JP Assist's recent dialogue history." });
    SPDLOG_INFO("[JPAssist] Registered plugin host companion (corpus={}, history, settings, diagnostics)",
                JPAssist::StudyRepository_IsCorpusLoaded() ? JPAssist::StudyRepository_GetCorpusVersion()
                                                           : "unavailable");
}

static RegisterShipInitFunc initFunc(RegisterJPAssist);

} // namespace

namespace JPAssist {

RuntimeStatus JPAssist_GetRuntimeStatus() {
    RuntimeStatus status;
    if (PluginRuntimeEnabled()) {
        const auto diagnostic = [](const char* field, int fallback = 0) {
            return CVarGetInteger((std::string(CVAR_ENHANCEMENT("StudyMods.jp-assist.Diagnostics.")) + field).c_str(),
                                  fallback);
        };
        status.textId = static_cast<uint16_t>(diagnostic("TextId", 0xFFFF));
        status.pageIndex = diagnostic("PageIndex");
        status.requestedLanguage = LANGUAGE_JPN;
        status.studyModeActive = diagnostic("Active") != 0;
        status.definitionVisible = diagnostic("DefinitionVisible", 1) != 0;
        status.choiceSelectionFrozen = diagnostic("ChoiceFrozen") != 0;
        status.choiceIndex = static_cast<uint8_t>(std::clamp(diagnostic("ChoiceIndex"), 0, 255));
        status.selectedTokenIndex = diagnostic("SelectedIndex");
        status.currentPageTokenCount = diagnostic("TokenCount");
        status.currentPageIsChoice = diagnostic("IsChoice") != 0;
        status.selectedTokenSaved = diagnostic("Saved") != 0;
        status.selectedTokenKnown = diagnostic("Known") != 0;
        status.selectedTokenAudioAvailable = diagnostic("AudioAvailable") != 0;
        status.studyEnterCount = diagnostic("EnterCount");
        status.studyNavigationCount = diagnostic("NavigationCount");
        status.studyScrollCount = diagnostic("ScrollCount");
        status.saveToggleCount = diagnostic("SaveCount");
        status.knownMarkCount = diagnostic("KnownCount");
        status.definitionToggleCount = diagnostic("DefinitionCount");
        status.audioPlayCount = diagnostic("AudioCount");
        status.displayMode = DialogueStudy::DialogueDisplayMode::JapaneseOnly;
        status.dialogueSurface = DialogueStudy::DialogueSurface::Hidden;
        return status;
    }
    // No source-integrated fallback remains. An unavailable plugin produces
    // an intentionally inactive status so automated validation fails clearly
    // instead of accidentally exercising a second implementation.
    status.textId = 0xFFFF;
    status.pageIndex = 0;
    status.requestedLanguage = LANGUAGE_JPN;
    status.alternateLanguageVisible = false;
    status.studyModeActive = false;
    status.definitionVisible = true;
    status.choiceSelectionFrozen = false;
    status.selectedTokenIndex = 0;
    status.displayMode = DialogueStudy::DialogueDisplayMode::JapaneseOnly;
    status.dialogueSurface = DialogueStudy::DialogueSurface::Hidden;
    status.displayModeFallback = false;
    status.languageToggleCount = 0;
    return status;
}

void JPAssist_QueueTestInput(uint16_t buttons, int8_t stickY, bool hasStickY) {
    StudyModApi::QueueNativeInputForTesting(buttons, stickY, hasStickY);
}

} // namespace JPAssist
