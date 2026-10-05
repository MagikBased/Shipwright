#include "StudyModApiInternal.h"

#include <algorithm>
#include <cstdint>

#include "soh/Enhancements/JPAssist/NativePageTracker.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "mods/study_mod_game_bridge.h"
#include "global.h"
#include "variables.h"

extern PlayState* gPlayState;

namespace {

bool sDialogueOpen = false;
uint16_t sDialogueId = 0;
JPAssist::NativePageTracker sPageTracker;
uint16_t sQueuedTestButtons = 0;
int8_t sQueuedTestStickY = 0;
bool sQueuedTestHasStickY = false;

uint32_t ToStudyLanguage(uint8_t language) {
    switch (language) {
        case LANGUAGE_ENG:
            return STUDY_MOD_LANGUAGE_ENGLISH;
        case LANGUAGE_JPN:
            return STUDY_MOD_LANGUAGE_JAPANESE;
        case LANGUAGE_GER:
            return STUDY_MOD_LANGUAGE_GERMAN;
        case LANGUAGE_FRA:
            return STUDY_MOD_LANGUAGE_FRENCH;
        default:
            return STUDY_MOD_LANGUAGE_UNKNOWN;
    }
}

bool IsDecodedPageReady(const MessageContext& message) {
    return message.msgMode != MSGMODE_NONE && message.msgMode != MSGMODE_TEXT_START &&
           message.msgMode != MSGMODE_TEXT_BOX_GROWING && message.msgMode != MSGMODE_TEXT_STARTING &&
           message.msgMode != MSGMODE_TEXT_NEXT_MSG && message.msgMode != MSGMODE_TEXT_CONTINUING;
}

bool IsDialogueActive() {
    return gPlayState != nullptr && gPlayState->msgCtx.msgLength != 0 &&
           gPlayState->msgCtx.msgMode != MSGMODE_NONE;
}

StudyModRect GetTextboxBounds() {
    StudyModNativeTextboxBounds nativeBounds{};
    if (!StudyModHost_GetNativeTextboxBounds(&nativeBounds)) {
        return {};
    }
    return {
        0.0f,
        static_cast<float>(nativeBounds.y),
        320.0f,
        static_cast<float>(nativeBounds.height),
        320.0f,
        static_cast<float>(nativeBounds.logical_screen_height),
    };
}

StudyModDialogueEvent MakeDialogueEvent(uint32_t type, const MessageContext& message, int pageIndex,
                                        bool decodedPageReady) {
    const bool isChoice = message.choiceNum > 1;
    return {
        sizeof(StudyModDialogueEvent),
        type,
        message.textId,
        static_cast<uint32_t>(std::max(0, pageIndex)),
        isChoice ? static_cast<int32_t>(message.choiceIndex) : -1,
        isChoice ? static_cast<uint32_t>(message.choiceNum) : 0,
        ToStudyLanguage(gSaveContext.language),
        (decodedPageReady ? static_cast<uint32_t>(STUDY_MOD_DIALOGUE_FLAG_PAGE_DECODED) : 0U) |
            (isChoice ? static_cast<uint32_t>(STUDY_MOD_DIALOGUE_FLAG_CHOICE) : 0U),
        GetTextboxBounds(),
    };
}

void CloseDialogue() {
    if (!sDialogueOpen) {
        return;
    }
    MessageContext empty{};
    empty.textId = sDialogueId;
    StudyModApi::DispatchDialogue(
        MakeDialogueEvent(STUDY_MOD_DIALOGUE_CLOSED, empty, sPageTracker.GetPageIndex(), false));
    sDialogueOpen = false;
    sDialogueId = 0;
    sPageTracker.Reset();
}

uint64_t NativeButtonsToStudy(CONTROLLERBUTTONS_T buttons) {
    uint64_t result = 0;
    if (buttons & BTN_A) result |= STUDY_MOD_INPUT_PRIMARY;
    if (buttons & BTN_B) result |= STUDY_MOD_INPUT_SECONDARY;
    if (buttons & BTN_START) result |= STUDY_MOD_INPUT_MENU;
    if (buttons & BTN_L) result |= STUDY_MOD_INPUT_LEFT_SHOULDER;
    if (buttons & BTN_R) result |= STUDY_MOD_INPUT_RIGHT_SHOULDER;
    if (buttons & BTN_Z) result |= STUDY_MOD_INPUT_LEFT_TRIGGER;
    if (buttons & BTN_CUP) result |= STUDY_MOD_INPUT_FACE_UP;
    if (buttons & BTN_CDOWN) result |= STUDY_MOD_INPUT_FACE_DOWN;
    if (buttons & BTN_CLEFT) result |= STUDY_MOD_INPUT_FACE_LEFT;
    if (buttons & BTN_CRIGHT) result |= STUDY_MOD_INPUT_FACE_RIGHT;
    if (buttons & BTN_DUP) result |= STUDY_MOD_INPUT_NAV_UP;
    if (buttons & BTN_DDOWN) result |= STUDY_MOD_INPUT_NAV_DOWN;
    if (buttons & BTN_DLEFT) result |= STUDY_MOD_INPUT_NAV_LEFT;
    if (buttons & BTN_DRIGHT) result |= STUDY_MOD_INPUT_NAV_RIGHT;
    return result;
}

CONTROLLERBUTTONS_T StudyButtonsToNative(uint64_t buttons) {
    CONTROLLERBUTTONS_T result = 0;
    if (buttons & STUDY_MOD_INPUT_PRIMARY) result |= BTN_A;
    if (buttons & STUDY_MOD_INPUT_SECONDARY) result |= BTN_B;
    if (buttons & STUDY_MOD_INPUT_MENU) result |= BTN_START;
    if (buttons & STUDY_MOD_INPUT_LEFT_SHOULDER) result |= BTN_L;
    if (buttons & STUDY_MOD_INPUT_RIGHT_SHOULDER) result |= BTN_R;
    if (buttons & STUDY_MOD_INPUT_LEFT_TRIGGER) result |= BTN_Z;
    if (buttons & STUDY_MOD_INPUT_FACE_UP) result |= BTN_CUP;
    if (buttons & STUDY_MOD_INPUT_FACE_DOWN) result |= BTN_CDOWN;
    if (buttons & STUDY_MOD_INPUT_FACE_LEFT) result |= BTN_CLEFT;
    if (buttons & STUDY_MOD_INPUT_FACE_RIGHT) result |= BTN_CRIGHT;
    if (buttons & STUDY_MOD_INPUT_NAV_UP) result |= BTN_DUP;
    if (buttons & STUDY_MOD_INPUT_NAV_DOWN) result |= BTN_DDOWN;
    if (buttons & STUDY_MOD_INPUT_NAV_LEFT) result |= BTN_DLEFT;
    if (buttons & STUDY_MOD_INPUT_NAV_RIGHT) result |= BTN_DRIGHT;
    return result;
}

void DispatchInput(Input& input) {
    input.press.button |= sQueuedTestButtons;
    if (sQueuedTestHasStickY) {
        input.rel.stick_y = sQueuedTestStickY;
    }
    sQueuedTestButtons = 0;
    sQueuedTestStickY = 0;
    sQueuedTestHasStickY = false;
    StudyModInputEvent event{
        sizeof(StudyModInputEvent),
        STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
        NativeButtonsToStudy(input.press.button),
        NativeButtonsToStudy(input.cur.button),
        NativeButtonsToStudy(input.rel.button),
        input.rel.stick_x,
        input.rel.stick_y,
    };
    const uint64_t consumedStudyInput = StudyModApi::DispatchInput(event);
    const CONTROLLERBUTTONS_T consumed = StudyButtonsToNative(consumedStudyInput);
    input.press.button &= ~consumed;
    input.cur.button &= ~consumed;
    if ((consumedStudyInput & STUDY_MOD_INPUT_CONSUME_STICK_X) != 0) {
        input.rel.stick_x = 0;
    }
    if ((consumedStudyInput & STUDY_MOD_INPUT_CONSUME_STICK_Y) != 0) {
        input.rel.stick_y = 0;
    }
}

void OnDialogMessage() {
    if (!IsDialogueActive()) {
        CloseDialogue();
        return;
    }

    MessageContext& message = gPlayState->msgCtx;
    if (message.msgMode == MSGMODE_TEXT_CLOSING) {
        CloseDialogue();
        return;
    }

    if (!sDialogueOpen || message.textId != sDialogueId) {
        CloseDialogue();
        sDialogueOpen = true;
        sDialogueId = message.textId;
        sPageTracker.Reset();
        StudyModApi::DispatchDialogue(MakeDialogueEvent(STUDY_MOD_DIALOGUE_OPENED, message, 0, false));
    }

    const bool decodedPageReady = IsDecodedPageReady(message);
    int pageIndex = sPageTracker.GetPageIndex();
    if (decodedPageReady && sPageTracker.Observe(StudyModHost_GetNativeTextBoxNumber(), pageIndex)) {
        StudyModApi::DispatchDialogue(
            MakeDialogueEvent(STUDY_MOD_DIALOGUE_PAGE_CHANGED, message, pageIndex, true));
    }
    StudyModApi::DispatchDialogue(
        MakeDialogueEvent(STUDY_MOD_DIALOGUE_UPDATED, message, pageIndex, decodedPageReady));
    DispatchInput(gPlayState->state.input[0]);
}

void OnSceneChanged(int16_t sceneNumber) {
    CloseDialogue();
    const StudyModLifecycleEvent event{ sizeof(StudyModLifecycleEvent), STUDY_MOD_LIFECYCLE_SCENE_CHANGED,
                                        sceneNumber };
    StudyModApi::DispatchLifecycle(event);
}

void OnGameExited(int32_t fileNumber) {
    CloseDialogue();
    const StudyModLifecycleEvent event{ sizeof(StudyModLifecycleEvent), STUDY_MOD_LIFECYCLE_GAME_EXITED, fileNumber };
    StudyModApi::DispatchLifecycle(event);
}

void RegisterStudyModBridge() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDialogMessage>(OnDialogMessage);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (!IsDialogueActive()) {
            CloseDialogue();
        }
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneChanged);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>(OnGameExited);
}

static RegisterShipInitFunc sRegisterStudyModBridge(RegisterStudyModBridge);

} // namespace

namespace StudyModApi {

void QueueNativeInputForTesting(uint16_t buttons, int8_t stickY, bool hasStickY) {
    sQueuedTestButtons |= buttons;
    if (hasStickY) {
        sQueuedTestStickY = stickY;
        sQueuedTestHasStickY = true;
    }
}

} // namespace StudyModApi
