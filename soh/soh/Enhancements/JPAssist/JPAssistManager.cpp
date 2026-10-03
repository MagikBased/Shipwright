#include "JPAssistManager.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "DialogueRepository.h"
#include "DialoguePresentation.h"
#include "JPAssistHistory.h"
#include "JPAssistAudio.h"
#include "JPAssistNativeHighlight.h"
#include "JPAssistOverlay.h"
#include "JPAssistTestLab.h"
#include "LearningSyncRuntime.h"
#include "MessageParser.h"
#include "NativePageTracker.h"
#include "StudyPersistence.h"
#include "StudyRepository.h"
#include "StudySelectionMemory.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "z64.h"

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/GameOverlay.h>

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

// Dialogue tracking, reset whenever a new message opens.
uint16_t sTrackedTextId = 0xFFFF;
uint8_t sLastMsgMode = MSGMODE_NONE;
int sCurrentPageIndex = 0;
JPAssist::NativePageTracker sNativePageTracker;

// Study Mode selection is an occurrence index within the current corpus page.
bool sStudyModeActive = false;
int sSelectedTokenIndex = 0;
JPAssist::StudySelectionMemory sStudySelectionMemory;
uint64_t sStudyEnterCount = 0;
uint64_t sStudyNavigationCount = 0;
uint64_t sStudyScrollCount = 0;
uint64_t sSaveToggleCount = 0;
uint64_t sAudioPlayCount = 0;
bool sFrozenChoiceValid = false;
uint8_t sFrozenChoiceIndex = 0;
uint16_t sFrozenChoiceTextId = 0xFFFF;
int sFrozenChoicePageIndex = -1;
uint16_t sQueuedTestButtons = 0;
int8_t sQueuedTestStickY = 0;
bool sQueuedTestHasStickY = false;

bool sRomCompatibilityChecked = false;

std::shared_ptr<Ship::GameOverlay> GetOverlay() {
    return Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay();
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

const JPAssist::StudyPage* CurrentStudyPage() {
    return JPAssist::StudyRepository_FindPage(sTrackedTextId, sCurrentPageIndex);
}

void RememberCurrentSelection() {
    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page != nullptr) {
        sStudySelectionMemory.Remember(sTrackedTextId, sCurrentPageIndex, sSelectedTokenIndex,
                                       static_cast<int>(page->tokens.size()));
    }
}

void RestoreCurrentSelection() {
    const JPAssist::StudyPage* page = CurrentStudyPage();
    sSelectedTokenIndex = page == nullptr
                              ? 0
                              : sStudySelectionMemory.Restore(sTrackedTextId, sCurrentPageIndex,
                                                              static_cast<int>(page->tokens.size()));
}

void ExitStudyMode() {
    if (!sStudyModeActive) {
        return;
    }
    RememberCurrentSelection();
    sStudyModeActive = false;
    sFrozenChoiceValid = false;
    JPAssist::JPAssistOverlay_Hide();
    // Flush encounter counts accumulated while navigating (design doc
    // section 9). Saved-word toggles (C-Right) already save immediately
    // since that's an explicit, infrequent action - batching the
    // once-per-navigation encounter counter here instead avoids a disk
    // write on every single D-pad press.
    JPAssist::StudyPersistence_Save();
    SPDLOG_INFO("[JPAssist] Study Mode exited");
}

// Dialogue callbacks stop entirely once the native textbox disappears. Keep
// teardown in one idempotent path so settings resets, save/scene teardown,
// forced textbox closure, and the ordinary closing state cannot leave an
// input-less overlay behind.
void ClearDialogueRuntimeState() {
    ExitStudyMode();
    // Hide defensively even when the manager already believed Study Mode was
    // inactive. The render-side overlay is intentionally independent and a
    // prior interrupted frame may otherwise have left it visible.
    JPAssist::JPAssistOverlay_Hide();
    sTrackedTextId = 0xFFFF;
    sLastMsgMode = MSGMODE_NONE;
    sCurrentPageIndex = 0;
    sNativePageTracker.Reset();
    sFrozenChoiceValid = false;
    sFrozenChoiceTextId = 0xFFFF;
    sFrozenChoicePageIndex = -1;
    sQueuedTestButtons = 0;
    sQueuedTestStickY = 0;
    sQueuedTestHasStickY = false;
}

void MaintainDialogueRuntimeState() {
    const bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.Enabled"), 1) != 0;
    // Message_Update returns before OnDialogMessage when msgLength is zero,
    // even if a forced reset left msgMode carrying its previous value.
    const bool dialogueActive = gPlayState != nullptr && gPlayState->msgCtx.msgLength != 0 &&
                                gPlayState->msgCtx.msgMode != MSGMODE_NONE;
    if (!enabled || !dialogueActive) {
        ClearDialogueRuntimeState();
    }
}

// Called whenever sSelectedTokenIndex changes (entering Study Mode counts as
// the first selection). Records one encounter for the newly-selected token -
// deliberately not called from DrawStudyCard, which runs every frame Study
// Mode is active and would otherwise increment the count dozens of times
// per second just for staying on the same token.
void RecordTokenEncounter(int index) {
    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page == nullptr || page->tokens.empty()) {
        return;
    }
    const auto& tokens = page->tokens;
    index = std::min(index, static_cast<int>(tokens.size()) - 1);
    JPAssist::StudyPersistence_RecordEncounter(tokens[index].Id());
    JPAssist::LearningSync_RecordWordEvent("word_encountered", tokens[index], sTrackedTextId, sCurrentPageIndex);
}

// Redraws the card for the currently selected token every frame Study Mode
// is active, rather than only on discrete navigation events. Milestone 1's
// language overlay learned this the hard way: reposting only on specific
// detected transitions (page-advance, toggle-press) can go stale if a
// transition is missed or races the render thread. Refreshing continuously
// from current state sidesteps that class of bug entirely - the display is
// never more than one frame behind whatever sSelectedTokenIndex actually is.
void DrawStudyCard() {
    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page == nullptr || page->tokens.empty()) {
        JPAssist::JPAssistOverlay_Hide();
        return;
    }
    const auto& tokens = page->tokens;
    int index = std::min(sSelectedTokenIndex, static_cast<int>(tokens.size()) - 1);

    JPAssist::JPAssistOverlay_ShowStudy(*page, index, JPAssist::JPAssistAudio_HasWord(tokens[index].Id()));
}

// Handles Study Mode's own input and, while active, consumes the buttons
// the native message system would otherwise read this same frame -
// GameInteractor::OnDialogMessage fires before Message_Update's msgMode
// switch (z_message_PAL.c:4440), so clearing these bits here reliably
// blocks Message_ShouldAdvance (z_message_PAL.c:161) from seeing them later
// in the same frame. This is the same input-consumption pattern
// soh/soh/Enhancements/Items/ArrowCycle.cpp:179-180 uses to suppress shield
// input while cycling arrows - clearing press/cur bits on the shared Input
// struct rather than trying to intercept the read.
void HandleStudyModeInput(PlayState* play, MessageContext* msgCtx, Input* input) {
    bool rPressed = CHECK_BTN_ALL(input->press.button, BTN_R);

    if (!sStudyModeActive) {
        // Study Mode is available whenever Japanese token data exists. On
        // choice pages the highlighted answer is captured below and frozen
        // while the study panel owns the D-pad/analog focus.
        const JPAssist::StudyPage* page = CurrentStudyPage();
        if (rPressed && page != nullptr && !page->tokens.empty()) {
            sStudyModeActive = true;
            sStudyEnterCount++;
            RestoreCurrentSelection();
            if (page->isChoice) {
                sFrozenChoiceValid = true;
                sFrozenChoiceIndex = msgCtx->choiceIndex;
                sFrozenChoiceTextId = sTrackedTextId;
                sFrozenChoicePageIndex = sCurrentPageIndex;
                input->rel.stick_y = 0;
                input->press.button &= ~(BTN_DUP | BTN_DDOWN);
                input->cur.button &= ~(BTN_DUP | BTN_DDOWN);
            }
            RecordTokenEncounter(sSelectedTokenIndex);
            JPAssist::LearningSync_RecordDialogueEvent("study_mode_opened", sTrackedTextId, sCurrentPageIndex);
            input->press.button &= ~BTN_R;
            input->cur.button &= ~BTN_R;
            DrawStudyCard();
            SPDLOG_INFO("[JPAssist] Study Mode entered");
        }
        return;
    }

    if (rPressed || CHECK_BTN_ALL(input->press.button, BTN_B)) {
        ExitStudyMode();
        input->press.button &= ~(BTN_R | BTN_B);
        input->cur.button &= ~(BTN_R | BTN_B);
        return;
    }

    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page != nullptr && page->isChoice) {
        if (!sFrozenChoiceValid || sFrozenChoiceTextId != sTrackedTextId ||
            sFrozenChoicePageIndex != sCurrentPageIndex) {
            sFrozenChoiceValid = true;
            sFrozenChoiceIndex = msgCtx->choiceIndex;
            sFrozenChoiceTextId = sTrackedTextId;
            sFrozenChoicePageIndex = sCurrentPageIndex;
        }
        // Message_HandleChoiceSelection runs later in Message_Update and
        // reads rel.stick_y plus D-up/down. Neutralize both, and restore the
        // captured index defensively in case another hook changed it.
        msgCtx->choiceIndex = sFrozenChoiceIndex;
        input->rel.stick_y = 0;
    }
    if (page != nullptr && !page->tokens.empty()) {
        const auto& tokens = page->tokens;
        int previousIndex = sSelectedTokenIndex;
        if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            sSelectedTokenIndex = std::min(sSelectedTokenIndex + 1, static_cast<int>(tokens.size()) - 1);
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            sSelectedTokenIndex = std::max(sSelectedTokenIndex - 1, 0);
        }
        if (sSelectedTokenIndex != previousIndex) {
            sStudyNavigationCount++;
            RememberCurrentSelection();
            RecordTokenEncounter(sSelectedTokenIndex);
        }

        if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            sStudyScrollCount++;
            JPAssist::JPAssistOverlay_ScrollStudy(-80.0f);
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            sStudyScrollCount++;
            JPAssist::JPAssistOverlay_ScrollStudy(80.0f);
        }

        // "C-Right: add or remove the word from the study list" (design
        // doc 4.3). Saved immediately, unlike encounter counts, since it's
        // an explicit and infrequent action rather than something that
        // fires on every navigation press.
        if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) {
            sSaveToggleCount++;
            int index = std::min(sSelectedTokenIndex, static_cast<int>(tokens.size()) - 1);
            const std::string& tokenId = tokens[index].Id();
            JPAssist::StudyPersistence_ToggleSaved(tokenId);
            JPAssist::StudyPersistence_Save();
            const bool saved = JPAssist::StudyPersistence_IsSaved(tokenId);
            JPAssist::LearningSync_RecordWordEvent(saved ? "word_saved" : "word_unsaved", tokens[index],
                                                   sTrackedTextId, sCurrentPageIndex);
            SPDLOG_INFO("[JPAssist] Token {} {}", tokenId,
                        saved ? "saved" : "unsaved");
        }

        // C-Left plays the reviewed pronunciation when the optional local
        // audio bundle contains this stable word identity. Missing audio is
        // intentionally a no-op so text-only installations behave exactly
        // as before.
        if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) {
            const int index = std::min(sSelectedTokenIndex, static_cast<int>(tokens.size()) - 1);
            if (JPAssist::JPAssistAudio_PlayWord(tokens[index].Id())) {
                sAudioPlayCount++;
                SPDLOG_INFO("[JPAssist] Playing pronunciation for {}", tokens[index].Id());
            }
        }
    }

    // Consume only Study Mode's own controls. A and C-Up deliberately remain
    // untouched so Message_ShouldAdvance can reveal/advance the native text
    // while the card stays open and follows the newly decoded page. B is our
    // close binding above; keeping it in this defensive mask also covers the
    // SkipText branch, which reads cur.button rather than only press.button.
    constexpr uint16_t studyOwnedButtons =
        BTN_B | BTN_R | BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_CLEFT | BTN_CRIGHT;
    input->press.button &= ~studyOwnedButtons;
    input->cur.button &= ~studyOwnedButtons;

    DrawStudyCard();
}

// Design doc section 9 / section 11's "dialogue history": records page 0's
// English text for whatever textId just opened. Prefer the normalized corpus,
// which retains choice text; use the raw-table parser only as a fallback.
void RecordHistoryForOpenedMessage(uint16_t textId) {
    // Account sync receives stable IDs and counts only. The rendered Japanese
    // and English dialogue remains in the game's local corpus/history.
    JPAssist::LearningSync_RecordDialogueEvent("dialogue_seen", textId, 0);
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
    // Design doc section 10's first settings entry: "Enable JP Assist." A
    // disabled mod should behave as if it isn't installed at all, not just
    // stop reacting to input - bailing out before any of the tracking
    // below runs means msgMode/textId state isn't even observed, so
    // there's nothing left to clean up if the player re-enables mid-message.
    if (!CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.Enabled"), 1)) {
        ClearDialogueRuntimeState();
        return;
    }

    CheckRomCompatibilityOnce();

    PlayState* play = gPlayState;
    MessageContext* msgCtx = &play->msgCtx;

    uint8_t msgMode = msgCtx->msgMode;

    if (msgCtx->textId != sTrackedTextId) {
        // Covers both a genuinely new conversation AND a TEXTID control-code
        // jump mid-message (soh/include/message_data_fmt.h CTRL_TEXTID) -
        // the latter doesn't reliably pass through MSGMODE_TEXT_START, so
        // keying off the id itself (rather than trying to enumerate every
        // mode that can precede it) is what actually catches it. Found by
        // hitting exactly this gap live: the page counter kept climbing
        // across an id change that a mode-only check had missed.
        if (sStudyModeActive) {
            RememberCurrentSelection();
        }
        sTrackedTextId = msgCtx->textId;
        sCurrentPageIndex = 0;
        sNativePageTracker.Reset();
        // A TEXTID jump is part of the active conversation, so preserve an
        // open Study card and retarget it to the new message's first page.
        // A genuinely separate conversation has already passed through the
        // closing state below, which exits Study Mode.
        GetOverlay()->ClearNotifications();
        if (sStudyModeActive) {
            RestoreCurrentSelection();
            sFrozenChoiceValid = false;
            const JPAssist::StudyPage* page = CurrentStudyPage();
            if (page != nullptr && !page->tokens.empty()) {
                RecordTokenEncounter(0);
                DrawStudyCard();
            } else {
                ExitStudyMode();
            }
        } else {
            JPAssist::JPAssistOverlay_Hide();
        }
        RecordHistoryForOpenedMessage(sTrackedTextId);
        SPDLOG_INFO("[JPAssist] Dialogue opened: textId {:#x}", sTrackedTextId);
    }

    // The native decoder owns an authoritative 1-based textbox number. Use
    // it instead of inferring page turns from msgMode transitions: ordinary
    // BOX_BREAK pages can pass through TEXT_NEXT_MSG rather than
    // TEXT_CONTINUING, while TEXTID jumps and language re-decodes can produce
    // transitions that look like page turns but are not. Zero means the new
    // message has not decoded its first page yet and is intentionally ignored.
    int observedPageIndex = sCurrentPageIndex;
    const bool decodedPageReady = msgMode != MSGMODE_NONE && msgMode != MSGMODE_TEXT_START &&
                                  msgMode != MSGMODE_TEXT_BOX_GROWING && msgMode != MSGMODE_TEXT_STARTING &&
                                  msgMode != MSGMODE_TEXT_NEXT_MSG && msgMode != MSGMODE_TEXT_CONTINUING;
    if (decodedPageReady &&
        sNativePageTracker.Observe(JPAssist_GetNativeTextBoxNumber(), observedPageIndex)) {
        if (sStudyModeActive) {
            RememberCurrentSelection();
        }
        sCurrentPageIndex = observedPageIndex;
        SPDLOG_INFO("[JPAssist] Page changed: textId {:#x}, now page {}", sTrackedTextId, sCurrentPageIndex);
        if (sStudyModeActive) {
            RestoreCurrentSelection();
            RecordTokenEncounter(sSelectedTokenIndex);
        }
    }

    if (msgMode == MSGMODE_TEXT_CLOSING && sLastMsgMode != MSGMODE_TEXT_CLOSING) {
        SPDLOG_INFO("[JPAssist] Dialogue closed: textId {:#x}", sTrackedTextId);
        ClearDialogueRuntimeState();
        GetOverlay()->ClearNotifications();
        return;
    }

    sLastMsgMode = msgMode;

    Input* input = &play->state.input[0];

    if (sQueuedTestButtons != 0 || sQueuedTestHasStickY) {
        // Synthetic tests model a one-frame edge, not a held controller
        // state. Keeping these out of `cur` also prevents unrelated global
        // button-chord shortcuts from observing an impossible held chord.
        input->press.button |= sQueuedTestButtons;
        if (sQueuedTestHasStickY) {
            input->rel.stick_y = sQueuedTestStickY;
        }
        sQueuedTestButtons = 0;
        sQueuedTestStickY = 0;
        sQueuedTestHasStickY = false;
    }

    // Study Mode owns R/B, navigation, and save input while active. Native A
    // and C-Up advancement remains available, and page tracking above keeps
    // the card synchronized with the resulting dialogue.
    HandleStudyModeInput(play, msgCtx, input);
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
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
            "Master toggle for the R-button Study Mode language-learning tools. "
            "Disabling this leaves the game exactly as if the mod weren't installed."));
    SohGui::mSohMenu->AddWidget(path, "Study card scale: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("JPAssist.CardScale"))
        .Options(UIWidgets::FloatSliderOptions().Min(0.70f).Max(1.50f).Step(0.05f).DefaultValue(1.0f).Format("%.2f"));
    SohGui::mSohMenu->AddWidget(path, "Study card opacity: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("JPAssist.CardOpacity"))
        .Options(UIWidgets::FloatSliderOptions().Min(0.40f).Max(1.0f).Step(0.05f).DefaultValue(0.92f).Format("%.2f"));
    SohGui::mSohMenu->AddWidget(path, "Reset study card layouts", WIDGET_BUTTON)
        .Callback([](WidgetInfo&) {
            CVarClearBlock(CVAR_ENHANCEMENT("JPAssist.Layout."));
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
        .Callback([](WidgetInfo&) { JPAssist::LearningSync_Configure(); })
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
            JPAssist::LearningSync_Configure();
        }
    });
    SohGui::mSohMenu->AddWidget(path, "Connect learning account", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            const JPAssist::LearningSyncStatus status = JPAssist::LearningSync_GetStatus();
            info.options->disabled = !status.enabled || !status.transportAvailable || status.connected || status.pairing;
        })
        .Callback([](WidgetInfo&) { JPAssist::LearningSync_BeginPairing(); })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Request a short-lived code, then approve this game from the learning website. Your password is never "
            "entered into the game."));
    SohGui::mSohMenu->AddWidget(path, "LearningAccountStatus", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo&) {
            const JPAssist::LearningSyncStatus status = JPAssist::LearningSync_GetStatus();
            ImGui::TextWrapped("%s", status.message.empty() ? "Not connected" : status.message.c_str());
            if (status.pairing) {
                ImGui::Text("Code: %s", status.userCode.c_str());
                ImGui::TextWrapped("Open: %s", status.verificationUrl.c_str());
                if (UIWidgets::Button("Copy address and code##JPAssistPairing",
                                      UIWidgets::ButtonOptions().Color(THEME_COLOR))) {
                    const std::string clipboard = status.verificationUrl + "\n" + status.userCode;
                    ImGui::SetClipboardText(clipboard.c_str());
                }
            }
            if (status.pendingEventCount != 0) {
                ImGui::Text("Waiting to sync: %zu events", status.pendingEventCount);
            }
        })
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "Disconnect learning account", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            const JPAssist::LearningSyncStatus status = JPAssist::LearningSync_GetStatus();
            info.options->disabled = !status.connected && !status.pairing;
        })
        .Callback([](WidgetInfo&) { JPAssist::LearningSync_Disconnect(); });
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
    JPAssist::StudyRepository_LoadCorpus();
    JPAssist::JPAssistAudio_LoadManifest();
    JPAssist::StudyPersistence_Load();
    JPAssist::LearningSync_Initialize();
    JPAssist::JPAssistOverlay_Register();
    JPAssist::JPAssistHistory_Register();
    JPAssist::JPAssistTestLab_Register();
    if (!JPAssist::JPAssistOverlay_HasJapaneseFont()) {
        SPDLOG_WARN("[JPAssist] Shipwright's bundled Japanese font is unavailable; Japanese overlay text may render "
                    "with missing glyphs");
    }
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDialogMessage>(OnDialogMessage);
    // OnDialogMessage is not invoked after a textbox is removed, so this
    // frame hook owns fail-safe cleanup for settings resets and forced exits.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(MaintainDialogueRuntimeState);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        ClearDialogueRuntimeState();
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) {
        ClearDialogueRuntimeState();
    });
    RegisterJPAssistMenu();
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "jpassist_history", { JPAssistHistoryCommand, "Lists JP Assist's recent dialogue history." });
    SPDLOG_INFO("[JPAssist] Registered (corpus={}, R Study Mode, persistence, Anki export data)",
                JPAssist::StudyRepository_IsCorpusLoaded() ? JPAssist::StudyRepository_GetCorpusVersion()
                                                           : "unavailable");
}

static RegisterShipInitFunc initFunc(RegisterJPAssist);

} // namespace

extern "C" bool JPAssist_GetNativeHighlight(uint16_t textId, JPAssistNativeHighlight* highlight) {
    if (highlight == nullptr || !sStudyModeActive || textId != sTrackedTextId) {
        return false;
    }

    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page == nullptr || page->tokens.empty()) {
        return false;
    }

    const int index = std::clamp(sSelectedTokenIndex, 0, static_cast<int>(page->tokens.size()) - 1);
    const JPAssist::StudyToken& token = page->tokens[index];
    highlight->start = token.start;
    highlight->length = token.length;
    return token.length > 0;
}

namespace JPAssist {

RuntimeStatus JPAssist_GetRuntimeStatus() {
    RuntimeStatus status;
    status.textId = sTrackedTextId;
    status.pageIndex = sCurrentPageIndex;
    status.requestedLanguage = LANGUAGE_JPN;
    status.alternateLanguageVisible = false;
    status.studyModeActive = sStudyModeActive;
    status.choiceSelectionFrozen = sStudyModeActive && sFrozenChoiceValid;
    status.selectedTokenIndex = sSelectedTokenIndex;
    status.displayMode = DialogueStudy::DialogueDisplayMode::JapaneseOnly;
    status.dialogueSurface = DialogueStudy::DialogueSurface::Hidden;
    status.displayModeFallback = false;
    status.languageToggleCount = 0;
    status.studyEnterCount = sStudyEnterCount;
    status.studyNavigationCount = sStudyNavigationCount;
    status.studyScrollCount = sStudyScrollCount;
    status.saveToggleCount = sSaveToggleCount;
    status.audioPlayCount = sAudioPlayCount;
    if (const StudyPage* page = CurrentStudyPage(); page != nullptr) {
        status.currentPageTokenCount = static_cast<int>(page->tokens.size());
        status.currentPageIsChoice = page->isChoice;
        if (!page->tokens.empty()) {
            const int index = std::clamp(sSelectedTokenIndex, 0, static_cast<int>(page->tokens.size()) - 1);
            status.selectedTokenSaved = StudyPersistence_IsSaved(page->tokens[index].Id());
            status.selectedTokenAudioAvailable = JPAssistAudio_HasWord(page->tokens[index].Id());
        }
    }
    if (gPlayState != nullptr) {
        status.choiceIndex = gPlayState->msgCtx.choiceIndex;
    }
    return status;
}

void JPAssist_QueueTestInput(uint16_t buttons, int8_t stickY, bool hasStickY) {
    sQueuedTestButtons |= buttons;
    if (hasStickY) {
        sQueuedTestStickY = stickY;
        sQueuedTestHasStickY = true;
    }
}

} // namespace JPAssist
