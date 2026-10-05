#include "JPAssistManager.h"

#include <algorithm>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "DialogueRepository.h"
#include "DialoguePresentation.h"
#include "JPAssistHistory.h"
#include "JPAssistAudio.h"
#include "ShipwrightJPAssistHost.h"
#include "JPAssistNativeHighlight.h"
#include "JPAssistOverlay.h"
#include "JPAssistTestLab.h"
#include "LearningSyncRuntime.h"
#include "MessageParser.h"
#include "NativePageTracker.h"
#include "StudyPersistence.h"
#include "StudyRepository.h"
#include "StudySession.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/ModApi/StudyModApiInternal.h"
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

uint8_t sLastMsgMode = MSGMODE_NONE;
JPAssist::NativePageTracker sNativePageTracker;
JPAssist::StudySession sStudySession;
uint16_t sQueuedTestButtons = 0;
int8_t sQueuedTestStickY = 0;
bool sQueuedTestHasStickY = false;
uint16_t sLastPluginHistoryTextId = 0xFFFF;

bool sRomCompatibilityChecked = false;

bool PluginRuntimeEnabled() {
    return StudyModApi::HasRegisteredMod("jp-assist") &&
           CVarGetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.Diagnostics.RuntimeEnabled"), 0) != 0;
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
    return JPAssist::StudyRepository_FindPage(sStudySession.TextId(), sStudySession.PageIndex());
}

void RetargetStudySession(uint16_t textId, int pageIndex) {
    const JPAssist::StudyPage* page = JPAssist::StudyRepository_FindPage(textId, pageIndex);
    sStudySession.RetargetDialogue(textId, pageIndex, page == nullptr ? 0 : static_cast<int>(page->tokens.size()),
                                   page != nullptr && page->isChoice);
}

void FinishStudyModeExit() {
    JPAssist::JPAssistOverlay_Hide();
    JPAssist::StudyPersistence_Save();
    SPDLOG_INFO("[JPAssist] Study Mode exited");
}

void ExitStudyMode() {
    if (!sStudySession.Exit()) {
        return;
    }
    // Flush encounter counts accumulated while navigating (design doc
    // section 9). Saved-word toggles (C-Right) already save immediately
    // since that's an explicit, infrequent action - batching the
    // once-per-navigation encounter counter here instead avoids a disk
    // write on every single D-pad press.
    FinishStudyModeExit();
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
    sStudySession.ClearDialogue();
    sLastMsgMode = MSGMODE_NONE;
    sNativePageTracker.Reset();
    sQueuedTestButtons = 0;
    sQueuedTestStickY = 0;
    sQueuedTestHasStickY = false;
}

void MaintainDialogueRuntimeState() {
    if (PluginRuntimeEnabled()) {
        const bool dialogueActive = gPlayState != nullptr && gPlayState->msgCtx.msgLength != 0 &&
                                    gPlayState->msgCtx.msgMode != MSGMODE_NONE &&
                                    gPlayState->msgCtx.msgMode != MSGMODE_TEXT_CLOSING;
        if (!dialogueActive) {
            sLastPluginHistoryTextId = 0xFFFF;
        }
        ClearDialogueRuntimeState();
        return;
    }
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
    JPAssist::LearningSync_RecordWordEvent("word_encountered", tokens[index], sStudySession.TextId(),
                                           sStudySession.PageIndex());
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
    int index = std::min(sStudySession.SelectedTokenIndex(), static_cast<int>(tokens.size()) - 1);

    JPAssist::JPAssistOverlay_ShowStudy(
        *page, index, JPAssist::JPAssistAudio_HasWord(tokens[index].Id()),
        JPAssist::StudyPersistence_IsKnown(tokens[index].Id(), tokens[index].senseId),
        sStudySession.IsDefinitionVisible());
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
    const bool rPressed = CHECK_BTN_ALL(input->press.button, BTN_R);
    const bool definitionPressed = CHECK_BTN_ALL(input->press.button, BTN_L) ||
                                   CHECK_BTN_ALL(input->press.button, BTN_Z);
    JPAssist::StudyCommand commands = JPAssist::StudyCommand::None;
    if (rPressed) {
        commands = commands | (sStudySession.IsActive() ? JPAssist::StudyCommand::Close
                                                        : JPAssist::StudyCommand::OpenWithDefinition);
    }
    if (definitionPressed) {
        commands = commands | (sStudySession.IsActive() ? JPAssist::StudyCommand::ToggleDefinition
                                                        : JPAssist::StudyCommand::OpenRecallFirst);
    }
    if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) commands = commands | JPAssist::StudyCommand::NextWord;
    if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) commands = commands | JPAssist::StudyCommand::PreviousWord;
    if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) commands = commands | JPAssist::StudyCommand::ScrollUp;
    if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) commands = commands | JPAssist::StudyCommand::ScrollDown;
    if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) commands = commands | JPAssist::StudyCommand::ToggleSaved;
    if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) commands = commands | JPAssist::StudyCommand::MarkKnown;
    if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN)) commands = commands | JPAssist::StudyCommand::PlayAudio;

    const JPAssist::StudySessionResult result = sStudySession.HandleCommands(commands, msgCtx->choiceIndex);
    if (result.consumeEntryCommands) {
        constexpr uint16_t entryButtons = BTN_R | BTN_L | BTN_Z;
        input->press.button &= ~entryButtons;
        input->cur.button &= ~entryButtons;
    }
    if (result.entered) {
        if (result.freezeChoice) {
            msgCtx->choiceIndex = result.frozenChoiceIndex;
            input->rel.stick_y = 0;
            input->press.button &= ~(BTN_DUP | BTN_DDOWN);
            input->cur.button &= ~(BTN_DUP | BTN_DDOWN);
        }
        RecordTokenEncounter(sStudySession.SelectedTokenIndex());
        JPAssist::LearningSync_RecordDialogueEvent("study_mode_opened", sStudySession.TextId(),
                                                   sStudySession.PageIndex());
        DrawStudyCard();
        SPDLOG_INFO("[JPAssist] Study Mode entered with definition {}",
                    sStudySession.IsDefinitionVisible() ? "visible" : "hidden");
        return;
    }
    if (result.exited) {
        FinishStudyModeExit();
        input->press.button &= ~BTN_R;
        input->cur.button &= ~BTN_R;
        return;
    }
    if (!sStudySession.IsActive()) {
        return;
    }

    if (result.definitionToggled) {
        SPDLOG_INFO("[JPAssist] Definition {}", sStudySession.IsDefinitionVisible() ? "revealed" : "hidden");
    }
    if (result.freezeChoice) {
        // Message_HandleChoiceSelection runs later in Message_Update. Restore
        // the captured selection and neutralize its vertical controls.
        msgCtx->choiceIndex = result.frozenChoiceIndex;
        input->rel.stick_y = 0;
    }

    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page != nullptr && !page->tokens.empty()) {
        const auto& tokens = page->tokens;
        const int index = std::min(sStudySession.SelectedTokenIndex(), static_cast<int>(tokens.size()) - 1);
        if (result.selectionChanged) {
            RecordTokenEncounter(index);
        }
        if (result.scrollPixels != 0.0f) {
            JPAssist::JPAssistOverlay_ScrollStudy(result.scrollPixels);
        }
        if (result.toggleSaved) {
            const std::string tokenId = tokens[index].Id();
            JPAssist::StudyPersistence_ToggleSaved(tokenId);
            JPAssist::StudyPersistence_Save();
            const bool saved = JPAssist::StudyPersistence_IsSaved(tokenId);
            JPAssist::LearningSync_RecordWordEvent(saved ? "word_saved" : "word_unsaved", tokens[index],
                                                   sStudySession.TextId(), sStudySession.PageIndex());
            SPDLOG_INFO("[JPAssist] Token {} {}", tokenId, saved ? "saved" : "unsaved");
        }
        if (result.markKnown) {
            const std::string tokenId = tokens[index].Id();
            if (!JPAssist::StudyPersistence_IsKnown(tokenId, tokens[index].senseId)) {
                JPAssist::StudyPersistence_MarkKnown(tokenId, tokens[index].senseId);
                JPAssist::StudyPersistence_Save();
                JPAssist::LearningSync_RecordWordEvent("word_known", tokens[index], sStudySession.TextId(),
                                                       sStudySession.PageIndex());
                sStudySession.RecordKnownMarked();
                SPDLOG_INFO("[JPAssist] Token {} sense {} marked known", tokenId, tokens[index].senseId);
            }
        }
        if (result.playAudio && JPAssist::JPAssistAudio_PlayWord(tokens[index].Id())) {
            sStudySession.RecordAudioPlayed();
            SPDLOG_INFO("[JPAssist] Playing pronunciation for {}", tokens[index].Id());
        }
    }

    // Consume only Study Mode's own controls. A and C-Up deliberately remain
    // untouched so Message_ShouldAdvance can reveal/advance the native text
    // while the card stays open and follows the newly decoded page. R is the
    // sole close binding; B remains available to the native dialogue system.
    constexpr uint16_t studyOwnedButtons =
        BTN_R | BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN | BTN_L | BTN_Z;
    input->press.button &= ~studyOwnedButtons;
    input->cur.button &= ~studyOwnedButtons;

    DrawStudyCard();
}

// Design doc section 9 / section 11's "dialogue history": records page 0's
// English text for whatever textId just opened. Prefer the normalized corpus,
// which retains choice text; use the raw-table parser only as a fallback.
void RecordHistoryForOpenedMessage(uint16_t textId, bool recordLearningEvent = true) {
    // Account sync receives stable IDs and counts only. The rendered Japanese
    // and English dialogue remains in the game's local corpus/history.
    if (recordLearningEvent) {
        JPAssist::LearningSync_RecordDialogueEvent("dialogue_seen", textId, 0);
    }
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
        sLastPluginHistoryTextId = 0xFFFF;
        ClearDialogueRuntimeState();
        return;
    }

    if (PluginRuntimeEnabled()) {
        // Dialogue history is a Shipwright companion window rather than part
        // of the study-card runtime. Keep observing opens while the plugin is
        // active, but do not duplicate the plugin's account-sync event.
        if (gPlayState != nullptr && gPlayState->msgCtx.msgMode != MSGMODE_TEXT_CLOSING &&
            gPlayState->msgCtx.textId != sLastPluginHistoryTextId) {
            sLastPluginHistoryTextId = gPlayState->msgCtx.textId;
            RecordHistoryForOpenedMessage(sLastPluginHistoryTextId, false);
        }
        ClearDialogueRuntimeState();
        return;
    }
    sLastPluginHistoryTextId = 0xFFFF;

    CheckRomCompatibilityOnce();

    PlayState* play = gPlayState;
    MessageContext* msgCtx = &play->msgCtx;

    uint8_t msgMode = msgCtx->msgMode;

    if (msgCtx->textId != sStudySession.TextId()) {
        // Covers both a genuinely new conversation AND a TEXTID control-code
        // jump mid-message (soh/include/message_data_fmt.h CTRL_TEXTID) -
        // the latter doesn't reliably pass through MSGMODE_TEXT_START, so
        // keying off the id itself (rather than trying to enumerate every
        // mode that can precede it) is what actually catches it. Found by
        // hitting exactly this gap live: the page counter kept climbing
        // across an id change that a mode-only check had missed.
        const bool studyWasActive = sStudySession.IsActive();
        RetargetStudySession(msgCtx->textId, 0);
        sNativePageTracker.Reset();
        // A TEXTID jump is part of the active conversation, so preserve an
        // open Study card and retarget it to the new message's first page.
        // A genuinely separate conversation has already passed through the
        // closing state below, which exits Study Mode.
        GetOverlay()->ClearNotifications();
        if (studyWasActive) {
            const JPAssist::StudyPage* page = CurrentStudyPage();
            if (page != nullptr && !page->tokens.empty()) {
                RecordTokenEncounter(sStudySession.SelectedTokenIndex());
                DrawStudyCard();
            } else {
                ExitStudyMode();
            }
        } else {
            JPAssist::JPAssistOverlay_Hide();
        }
        RecordHistoryForOpenedMessage(sStudySession.TextId());
        SPDLOG_INFO("[JPAssist] Dialogue opened: textId {:#x}", sStudySession.TextId());
    }

    // The native decoder owns an authoritative 1-based textbox number. Use
    // it instead of inferring page turns from msgMode transitions: ordinary
    // BOX_BREAK pages can pass through TEXT_NEXT_MSG rather than
    // TEXT_CONTINUING, while TEXTID jumps and language re-decodes can produce
    // transitions that look like page turns but are not. Zero means the new
    // message has not decoded its first page yet and is intentionally ignored.
    int observedPageIndex = sStudySession.PageIndex();
    const bool decodedPageReady = msgMode != MSGMODE_NONE && msgMode != MSGMODE_TEXT_START &&
                                  msgMode != MSGMODE_TEXT_BOX_GROWING && msgMode != MSGMODE_TEXT_STARTING &&
                                  msgMode != MSGMODE_TEXT_NEXT_MSG && msgMode != MSGMODE_TEXT_CONTINUING;
    if (decodedPageReady &&
        sNativePageTracker.Observe(JPAssist_GetNativeTextBoxNumber(), observedPageIndex)) {
        const bool studyWasActive = sStudySession.IsActive();
        RetargetStudySession(sStudySession.TextId(), observedPageIndex);
        SPDLOG_INFO("[JPAssist] Page changed: textId {:#x}, now page {}", sStudySession.TextId(),
                    sStudySession.PageIndex());
        if (studyWasActive) {
            RecordTokenEncounter(sStudySession.SelectedTokenIndex());
        }
    }

    if (msgMode == MSGMODE_TEXT_CLOSING && sLastMsgMode != MSGMODE_TEXT_CLOSING) {
        SPDLOG_INFO("[JPAssist] Dialogue closed: textId {:#x}", sStudySession.TextId());
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

    // Study Mode owns R, definition, navigation, and study-action input while
    // active. Native A, B, and C-Up remain available, and page tracking above
    // keeps the card synchronized with the resulting dialogue.
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
        .Callback([](WidgetInfo&) { MirrorPluginSettings(); })
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
            "Master toggle for the R/L/Z Study Mode language-learning tools. "
            "Disabling this leaves the game exactly as if the mod weren't installed."));
    SohGui::mSohMenu->AddWidget(path, "JPAssistRuntimeStatus", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo&) {
            ImGui::TextDisabled("Runtime: %s", PluginRuntimeEnabled() ? "jp-assist.o2r plugin" : "built-in fallback");
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
        .Callback([](WidgetInfo&) {
            MirrorPluginSettings();
            JPAssist::LearningSync_Configure();
        })
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
            JPAssist::LearningSync_Configure();
        }
    });
    SohGui::mSohMenu->AddWidget(path, "Connect learning account", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            if (PluginRuntimeEnabled()) {
                const bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.AccountSync.Enabled"), 0) != 0;
                const bool transport = CVarGetInteger(
                                           CVAR_ENHANCEMENT(
                                               "StudyMods.jp-assist.AccountTransportAvailable"),
                                           0) != 0;
                const bool connected = CVarGetInteger(
                                           CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountConnected"), 0) != 0;
                const bool pairing = CVarGetInteger(
                                         CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPairing"), 0) != 0;
                info.options->disabled = !enabled || !transport || connected || pairing;
                return;
            }
            const JPAssist::LearningSyncStatus status = JPAssist::LearningSync_GetStatus();
            info.options->disabled = !status.enabled || !status.transportAvailable || status.connected || status.pairing;
        })
        .Callback([](WidgetInfo&) {
            if (PluginRuntimeEnabled()) {
                CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.BeginPairing"), 1);
            } else {
                JPAssist::LearningSync_BeginPairing();
            }
        })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Request a short-lived code, then approve this game from the learning website. Your password is never "
            "entered into the game."));
    SohGui::mSohMenu->AddWidget(path, "LearningAccountStatus", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo&) {
            if (PluginRuntimeEnabled()) {
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
                const int pending = CVarGetInteger(
                    CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPendingEvents"), 0);
                if (pending != 0) {
                    ImGui::Text("Waiting to sync: %d events", pending);
                }
                return;
            }
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
            if (PluginRuntimeEnabled()) {
                const bool connected = CVarGetInteger(
                                           CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountConnected"), 0) != 0;
                const bool pairing = CVarGetInteger(
                                         CVAR_ENHANCEMENT("StudyMods.jp-assist.AccountPairing"), 0) != 0;
                info.options->disabled = !connected && !pairing;
                return;
            }
            const JPAssist::LearningSyncStatus status = JPAssist::LearningSync_GetStatus();
            info.options->disabled = !status.connected && !status.pairing;
        })
        .Callback([](WidgetInfo&) {
            if (PluginRuntimeEnabled()) {
                CVarSetInteger(CVAR_ENHANCEMENT("StudyMods.jp-assist.Disconnect"), 1);
            } else {
                JPAssist::LearningSync_Disconnect();
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
    JPAssist::JPAssistAudio_LoadManifest();
    JPAssist::StudyPersistence_Load();
    JPAssist::LearningSync_Initialize();
    MigrateLegacyPluginState();
    MirrorPluginSettings();
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
    SPDLOG_INFO("[JPAssist] Registered (corpus={}, R/L/Z Study Mode, persistence, Anki export data)",
                JPAssist::StudyRepository_IsCorpusLoaded() ? JPAssist::StudyRepository_GetCorpusVersion()
                                                           : "unavailable");
}

static RegisterShipInitFunc initFunc(RegisterJPAssist);

} // namespace

extern "C" bool JPAssist_GetNativeHighlight(uint16_t textId, JPAssistNativeHighlight* highlight) {
    if (PluginRuntimeEnabled() || highlight == nullptr || !sStudySession.IsActive() ||
        textId != sStudySession.TextId()) {
        return false;
    }

    const JPAssist::StudyPage* page = CurrentStudyPage();
    if (page == nullptr || page->tokens.empty()) {
        return false;
    }

    const int index = std::clamp(sStudySession.SelectedTokenIndex(), 0, static_cast<int>(page->tokens.size()) - 1);
    const JPAssist::StudyToken& token = page->tokens[index];
    highlight->start = token.start;
    highlight->length = token.length;
    return token.length > 0;
}

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
    status.textId = sStudySession.TextId();
    status.pageIndex = sStudySession.PageIndex();
    status.requestedLanguage = LANGUAGE_JPN;
    status.alternateLanguageVisible = false;
    status.studyModeActive = sStudySession.IsActive();
    status.definitionVisible = sStudySession.IsDefinitionVisible();
    status.choiceSelectionFrozen = sStudySession.IsChoiceFrozen();
    status.selectedTokenIndex = sStudySession.SelectedTokenIndex();
    status.displayMode = DialogueStudy::DialogueDisplayMode::JapaneseOnly;
    status.dialogueSurface = DialogueStudy::DialogueSurface::Hidden;
    status.displayModeFallback = false;
    status.languageToggleCount = 0;
    const StudySessionCounters& counters = sStudySession.Counters();
    status.studyEnterCount = counters.enter;
    status.studyNavigationCount = counters.navigation;
    status.studyScrollCount = counters.scroll;
    status.saveToggleCount = counters.saveToggle;
    status.knownMarkCount = counters.knownMark;
    status.definitionToggleCount = counters.definitionToggle;
    status.audioPlayCount = counters.audioPlay;
    if (const StudyPage* page = CurrentStudyPage(); page != nullptr) {
        status.currentPageTokenCount = static_cast<int>(page->tokens.size());
        status.currentPageIsChoice = page->isChoice;
        if (!page->tokens.empty()) {
            const int index =
                std::clamp(sStudySession.SelectedTokenIndex(), 0, static_cast<int>(page->tokens.size()) - 1);
            status.selectedTokenSaved = StudyPersistence_IsSaved(page->tokens[index].Id());
            status.selectedTokenKnown =
                StudyPersistence_IsKnown(page->tokens[index].Id(), page->tokens[index].senseId);
            status.selectedTokenAudioAvailable = JPAssistAudio_HasWord(page->tokens[index].Id());
        }
    }
    if (gPlayState != nullptr) {
        status.choiceIndex = gPlayState->msgCtx.choiceIndex;
    }
    return status;
}

void JPAssist_QueueTestInput(uint16_t buttons, int8_t stickY, bool hasStickY) {
    if (PluginRuntimeEnabled()) {
        StudyModApi::QueueNativeInputForTesting(buttons, stickY, hasStickY);
        return;
    }
    sQueuedTestButtons |= buttons;
    if (hasStickY) {
        sQueuedTestStickY = stickY;
        sQueuedTestHasStickY = true;
    }
}

} // namespace JPAssist
