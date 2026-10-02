#include "JPAssistManager.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "DialogueRepository.h"
#include "JPAssistHistory.h"
#include "JPAssistOverlay.h"
#include "JPAssistTestLab.h"
#include "MessageParser.h"
#include "StudyPersistence.h"
#include "StudyRepository.h"

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
#include <soh/SohGui/UIWidgetOptions.hpp>
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

// Independent from gSaveContext.language (design doc section 4.2: display
// language must not leak into the player's menu-language setting). Starts
// following the save file's language until the player presses L/Z.
uint8_t sRequestedLanguage = LANGUAGE_ENG;
bool sRequestedLanguageInitialized = false;

// Dialogue tracking, reset whenever a new message opens.
uint16_t sTrackedTextId = 0xFFFF;
uint8_t sLastMsgMode = MSGMODE_NONE;
int sCurrentPageIndex = 0;
// Whether sTrackedTextId's own first page has been observed displaying yet.
// Needed because MSGMODE_TEXT_CONTINUING -> MSGMODE_TEXT_DISPLAYING isn't
// unique to "same-message page advanced": a TEXTID jump also routes its
// *first* page through Message_ContinueTextbox, so it produces the exact
// same transition. Without this guard, jumping to a new message got
// miscounted as "page 1" of that message instead of page 0 - caught live
// when toggling right after a jump reported the wrong page.
bool sFirstPageDisplayed = false;

// True while the overlay should keep reposting the alternate language as the
// player pages through - cleared on toggle-back or dialogue close.
bool sShowingAlternateLanguage = false;

// Study Mode selection is an occurrence index within the current corpus page.
bool sStudyModeActive = false;
int sSelectedTokenIndex = 0;
uint64_t sLanguageToggleCount = 0;
uint64_t sStudyEnterCount = 0;
uint64_t sStudyNavigationCount = 0;
uint64_t sSaveToggleCount = 0;
bool sFrozenChoiceValid = false;
uint8_t sFrozenChoiceIndex = 0;
uint16_t sFrozenChoiceTextId = 0xFFFF;
int sFrozenChoicePageIndex = -1;

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

void ExitStudyMode() {
    if (!sStudyModeActive) {
        return;
    }
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

const JPAssist::StudyPage* CurrentStudyPage() {
    return JPAssist::StudyRepository_FindPage(sTrackedTextId, sCurrentPageIndex);
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
    const JPAssist::StudyToken& token = tokens[index];

    // "Encounter count and saved/known status" (design doc 4.3's card
    // field list).
    bool saved = JPAssist::StudyPersistence_IsSaved(token.Id());
    int encounters = JPAssist::StudyPersistence_GetEncounterCount(token.Id());
    JPAssist::JPAssistOverlay_ShowStudy(*page, index, saved, encounters, sRequestedLanguage == LANGUAGE_ENG);
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
        if (rPressed && sRequestedLanguage == LANGUAGE_JPN && page != nullptr && !page->tokens.empty()) {
            sStudyModeActive = true;
            sStudyEnterCount++;
            sSelectedTokenIndex = 0;
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
            RecordTokenEncounter(sSelectedTokenIndex);
        }

        if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            JPAssist::JPAssistOverlay_ScrollStudy(-80.0f);
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
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
            SPDLOG_INFO("[JPAssist] Token {} {}", tokenId,
                        JPAssist::StudyPersistence_IsSaved(tokenId) ? "saved" : "unsaved");
        }
    }

    // Consume the native advance/close inputs so the conversation can't
    // progress while the study panel has focus (design doc 5). B is also
    // our own exit binding above, but it returns before reaching here, so
    // clearing it too is just defensive - Message_ShouldAdvance's
    // SkipText-cvar branch reads cur.button for B, not just press.button.
    input->press.button &= ~(BTN_A | BTN_B | BTN_CUP | BTN_R | BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_CRIGHT);
    input->cur.button &= ~(BTN_A | BTN_B | BTN_CUP | BTN_R | BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_CRIGHT);

    DrawStudyCard();
}

// Displays the normalized corpus page without mutating MessageContext. The
// native table parser remains an English-only fallback while a local corpus
// is absent or incomplete.
void PostAlternateLanguagePage(uint16_t textId, uint8_t language) {
    GetOverlay()->ClearNotifications();

    if (const JPAssist::StudyPage* page = JPAssist::StudyRepository_FindPage(textId, sCurrentPageIndex);
        page != nullptr) {
        const std::string& text = language == LANGUAGE_JPN ? page->japanese : page->english;
        if (!text.empty()) {
            JPAssist::JPAssistOverlay_ShowDialogue(language == LANGUAGE_JPN ? "Japanese" : "English", text);
            return;
        }
    }

    const char* segment = nullptr;
    uint32_t length = 0;

    if (!JPAssist::DialogueRepository_Find(textId, language, &segment, &length)) {
        SPDLOG_INFO("[JPAssist] textId {:#x}: no entry in the {} table (design doc 4.1: keep current text, "
                    "surface \"Translation unavailable\")",
                    textId, (language == LANGUAGE_JPN) ? "Japanese" : "English");
        GetOverlay()->TextDrawNotification(3.0f, true, "Translation unavailable");
        JPAssist::JPAssistOverlay_Hide();
        return;
    }

    JPAssist::DialogueStructure structure = JPAssist::MessageParser_Parse(segment, length, language);
    SPDLOG_INFO("[JPAssist] textId {:#x} in {}: {} page(s)", textId, (language == LANGUAGE_JPN) ? "Japanese" : "English",
                structure.pages.size());
    for (size_t i = 0; i < structure.pages.size(); i++) {
        const auto& page = structure.pages[i];
        if (page.isChoice) {
            SPDLOG_INFO("[JPAssist]   page {}: {}-choice", i, page.choiceCount);
        } else if (!page.englishText.empty()) {
            SPDLOG_INFO("[JPAssist]   page {}: \"{}\"", i, page.englishText);
        } else {
            SPDLOG_INFO("[JPAssist]   page {}", i);
        }
    }

    if (structure.pages.empty()) {
        return;
    }
    int clampedPage = std::min(sCurrentPageIndex, static_cast<int>(structure.pages.size()) - 1);
    const auto& page = structure.pages[clampedPage];

    if (language == LANGUAGE_JPN) {
        JPAssist::JPAssistOverlay_Hide();
        GetOverlay()->TextDrawNotification(4.0f, true, "%s",
                                           page.isChoice ? "(choice - text not yet decoded)"
                                                         : "(Japanese text not yet decoded)");
    } else if (page.isChoice) {
        JPAssist::JPAssistOverlay_Hide();
        GetOverlay()->TextDrawNotification(4.0f, true, "(choice)");
    } else if (!page.englishText.empty()) {
        JPAssist::JPAssistOverlay_ShowDialogue("English", page.englishText);
    }
}

void HandleLanguageTogglePress(PlayState* play) {
    MessageContext* msgCtx = &play->msgCtx;

    sRequestedLanguage = (sRequestedLanguage == LANGUAGE_JPN) ? LANGUAGE_ENG : LANGUAGE_JPN;
    sLanguageToggleCount++;
    SPDLOG_INFO("[JPAssist] Language Toggle pressed - now showing {} (textId {:#x}, page {})",
                (sRequestedLanguage == LANGUAGE_JPN) ? "Japanese" : "English", msgCtx->textId, sCurrentPageIndex);

    if (sRequestedLanguage == gSaveContext.language) {
        // Toggled back to whatever's natively rendering - nothing to post,
        // the real textbox already shows the right thing.
        sShowingAlternateLanguage = false;
        if (sStudyModeActive) {
            DrawStudyCard();
        } else {
            JPAssist::JPAssistOverlay_Hide();
        }
        return;
    }

    sShowingAlternateLanguage = true;
    if (sStudyModeActive) {
        DrawStudyCard();
    } else {
        PostAlternateLanguagePage(msgCtx->textId, sRequestedLanguage);
    }
}

// Design doc section 9 / section 11's "dialogue history": records page 0's
// English text for whatever textId just opened. Prefer the normalized corpus,
// which retains choice text; use the raw-table parser only as a fallback.
void RecordHistoryForOpenedMessage(uint16_t textId) {
    std::string text;
    if (const JPAssist::StudyPage* page = JPAssist::StudyRepository_FindPage(textId, 0);
        page != nullptr && !page->english.empty()) {
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
        JPAssist::JPAssistOverlay_Hide();
        return;
    }

    CheckRomCompatibilityOnce();

    PlayState* play = gPlayState;
    MessageContext* msgCtx = &play->msgCtx;

    if (!sRequestedLanguageInitialized) {
        sRequestedLanguage = gSaveContext.language;
        sRequestedLanguageInitialized = true;
    }

    uint8_t msgMode = msgCtx->msgMode;

    if (msgCtx->textId != sTrackedTextId) {
        // Covers both a genuinely new conversation AND a TEXTID control-code
        // jump mid-message (soh/include/message_data_fmt.h CTRL_TEXTID) -
        // the latter doesn't reliably pass through MSGMODE_TEXT_START, so
        // keying off the id itself (rather than trying to enumerate every
        // mode that can precede it) is what actually catches it. Found by
        // hitting exactly this gap live: the page counter kept climbing
        // across an id change that a mode-only check had missed.
        sTrackedTextId = msgCtx->textId;
        sCurrentPageIndex = 0;
        sFirstPageDisplayed = false;
        sShowingAlternateLanguage = false;
        // "A newly opened message resets token selection to the first
        // content word" / "Closing a textbox always closes Study Mode"
        // (design doc 5) - a TEXTID jump is as much "a newly opened
        // message" as a fresh conversation is, so exit here too rather
        // than leaving Study Mode attached to whatever the jump landed on.
        ExitStudyMode();
        // Clearing the tracking flag alone doesn't touch whatever toast is
        // still on screen: TextDrawNotification entries fade out on their
        // own timer regardless, so a jump mid-toggle left the *previous*
        // message's overlay text visibly lingering into the new one until
        // some later event happened to clear it - live-tested as "shows a
        // stale message before correcting itself."
        GetOverlay()->ClearNotifications();
        JPAssist::JPAssistOverlay_Hide();
        RecordHistoryForOpenedMessage(sTrackedTextId);
        SPDLOG_INFO("[JPAssist] Dialogue opened: textId {:#x}", sTrackedTextId);
    } else if (msgMode == MSGMODE_TEXT_DISPLAYING) {
        // Message_ContinueTextbox (soh/src/code/z_message_PAL.c:2873) is the
        // only place that sets MSGMODE_TEXT_CONTINUING, and it always resets
        // msgBufPos to 0 and re-decodes after a 3-frame stateTimer countdown
        // - the page doesn't actually change on screen until that countdown
        // hits zero and Message_Decode runs, flipping the mode to
        // MSGMODE_TEXT_DISPLAYING. tts.cpp's RegisterOnDialogMessageHook
        // (soh/soh/Enhancements/tts/tts.cpp:1028) fires a beat earlier, at
        // stateTimer==1, which is fine for queuing speech slightly ahead of
        // display - but reusing that same check here caused a real, visible
        // bug: pressing the toggle right around a page turn posted the
        // *next* page's text while the textbox was still showing the page
        // before it. Waiting for the actual mode flip fixes that.
        //
        // But CONTINUING -> DISPLAYING isn't unique to "next page of the
        // same message" either: a TEXTID jump's *first* page also runs
        // through Message_ContinueTextbox, producing this same transition.
        // sFirstPageDisplayed distinguishes "this id's own first page just
        // finished decoding" (no increment) from "a later page of an id
        // we've already been showing" (real advance) - found live when a
        // jump got miscounted as "page 1" of the target message instead of
        // its actual page 0.
        if (sLastMsgMode == MSGMODE_TEXT_CONTINUING && sFirstPageDisplayed) {
            sCurrentPageIndex++;
            SPDLOG_INFO("[JPAssist] Page advanced: textId {:#x}, now page {}", sTrackedTextId, sCurrentPageIndex);
            if (sShowingAlternateLanguage) {
                // Keep the overlay in sync with the page the player is
                // actually looking at, same page-index clamp as the initial
                // toggle.
                PostAlternateLanguagePage(sTrackedTextId, sRequestedLanguage);
            }
            if (sStudyModeActive) {
                // "A page change resets selection to the first token on
                // that page" (design doc 5).
                sSelectedTokenIndex = 0;
                RecordTokenEncounter(0);
            }
        }
        sFirstPageDisplayed = true;
    }

    if (msgMode == MSGMODE_TEXT_CLOSING && sLastMsgMode != MSGMODE_TEXT_CLOSING) {
        SPDLOG_INFO("[JPAssist] Dialogue closed: textId {:#x}", sTrackedTextId);
        sTrackedTextId = 0xFFFF;
        sShowingAlternateLanguage = false;
        ExitStudyMode();
        GetOverlay()->ClearNotifications();
        JPAssist::JPAssistOverlay_Hide();
    }

    sLastMsgMode = msgMode;

    Input* input = &play->state.input[0];

    // Study Mode's own input handling (R to enter/exit, D-pad to navigate)
    // and, while active, consuming A/B/C-up so the native textbox can't
    // advance underneath it. Must run before the L/Z check below reads
    // input, but L/Z itself is intentionally not gated on Study Mode being
    // active or inactive - design doc 4.3's Study Mode control table has
    // its own "L or Z: toggle the Japanese/English sentence without
    // leaving Study Mode" row, so the existing toggle handling already
    // does the right thing unmodified.
    HandleStudyModeInput(play, msgCtx, input);

    // Language input is only intercepted while dialogue is active (design
    // doc 4.1) - guaranteed here for free, since GameInteractor::OnDialogMessage
    // only fires while msgCtx->msgLength != 0 (z_message_PAL.c:4436).
    // "L and Z are interchangeable aliases... Individual aliases may still
    // be disabled in settings if another enhancement creates a conflict"
    // (design doc 4.1) - each gated by its own CVar rather than an
    // all-or-nothing toggle.
    bool lPressed = CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.EnableLAlias"), 1) &&
                    CHECK_BTN_ALL(input->press.button, BTN_L);
    bool zPressed = CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.EnableZAlias"), 1) &&
                    CHECK_BTN_ALL(input->press.button, BTN_Z);
    if (lPressed || zPressed) {
        HandleLanguageTogglePress(play);
    }
}

// Design doc section 10's settings list, as far as this spike implements
// it: master enable + the two alias toggles (Study Mode's own bindings
// aren't rebindable yet - that needs the input-editor integration real
// button remapping uses, out of scope here). Mirrors the pattern
// soh/soh/Enhancements/Presets/Presets.cpp:494 and several other
// independent enhancements use to add their own sidebar without touching
// soh/soh/SohGui/SohMenuEnhancements.cpp.
void RegisterJPAssistMenu() {
    WidgetPath path = { "Enhancements", "JP Assist", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddSidebarEntry("Enhancements", path.sidebarName, 1);

    SohGui::mSohMenu->AddWidget(path, "Enable JP Assist", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("JPAssist.Enabled"))
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
            "Master toggle for the JP Assist language-learning tools (L/Z language toggle, Study Mode). "
            "Disabling this leaves the game exactly as if the mod weren't installed."));
    SohGui::mSohMenu->AddWidget(path, "Enable L as a Language Toggle alias", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("JPAssist.EnableLAlias"))
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
            "L and Z are interchangeable aliases for the Language Toggle action. Disable one if it conflicts "
            "with another enhancement."));
    SohGui::mSohMenu->AddWidget(path, "Enable Z as a Language Toggle alias", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("JPAssist.EnableZAlias"))
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
            "L and Z are interchangeable aliases for the Language Toggle action. Disable one if it conflicts "
            "with another enhancement."));
    SohGui::mSohMenu->AddWidget(path, "Study card scale: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("JPAssist.CardScale"))
        .Options(UIWidgets::FloatSliderOptions().Min(0.70f).Max(1.50f).Step(0.05f).DefaultValue(1.0f).Format("%.2f"));
    SohGui::mSohMenu->AddWidget(path, "Study card opacity: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("JPAssist.CardOpacity"))
        .Options(UIWidgets::FloatSliderOptions().Min(0.40f).Max(1.0f).Step(0.05f).DefaultValue(0.92f).Format("%.2f"));
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
    JPAssist::StudyPersistence_Load();
    JPAssist::JPAssistOverlay_Register();
    JPAssist::JPAssistHistory_Register();
    JPAssist::JPAssistTestLab_Register();
    if (!JPAssist::JPAssistOverlay_HasJapaneseFont()) {
        SPDLOG_WARN("[JPAssist] Shipwright's bundled Japanese font is unavailable; Japanese overlay text may render "
                    "with missing glyphs");
    }
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDialogMessage>(OnDialogMessage);
    RegisterJPAssistMenu();
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "jpassist_history", { JPAssistHistoryCommand, "Lists JP Assist's recent dialogue history." });
    SPDLOG_INFO("[JPAssist] Registered (corpus={}, L/Z toggle, Study Mode, persistence, Anki export data)",
                JPAssist::StudyRepository_IsCorpusLoaded() ? JPAssist::StudyRepository_GetCorpusVersion()
                                                           : "unavailable");
}

static RegisterShipInitFunc initFunc(RegisterJPAssist);

} // namespace

namespace JPAssist {

RuntimeStatus JPAssist_GetRuntimeStatus() {
    RuntimeStatus status;
    status.textId = sTrackedTextId;
    status.pageIndex = sCurrentPageIndex;
    status.requestedLanguage = sRequestedLanguage;
    status.alternateLanguageVisible = sShowingAlternateLanguage;
    status.studyModeActive = sStudyModeActive;
    status.choiceSelectionFrozen = sStudyModeActive && sFrozenChoiceValid;
    status.selectedTokenIndex = sSelectedTokenIndex;
    status.languageToggleCount = sLanguageToggleCount;
    status.studyEnterCount = sStudyEnterCount;
    status.studyNavigationCount = sStudyNavigationCount;
    status.saveToggleCount = sSaveToggleCount;
    if (const StudyPage* page = CurrentStudyPage(); page != nullptr) {
        status.currentPageTokenCount = static_cast<int>(page->tokens.size());
        status.currentPageIsChoice = page->isChoice;
    }
    if (gPlayState != nullptr) {
        status.choiceIndex = gPlayState->msgCtx.choiceIndex;
    }
    return status;
}

} // namespace JPAssist
