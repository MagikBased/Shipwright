#include "LearningSyncRuntime.h"

#include <iomanip>
#include <memory>
#include <sstream>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

#include "StudyRepository.h"
#include "soh/cvar_prefixes.h"

namespace JPAssist {
namespace {

constexpr const char* kDefaultEndpoint = "http://127.0.0.1:8766";
constexpr const char* kGameId = "ocarina-of-time";
constexpr const char* kAdapterId = "ship-of-harkinian";

std::unique_ptr<LearningSyncClient> sClient;

std::string MessageId(uint16_t textId) {
    std::ostringstream value;
    value << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << textId;
    return value.str();
}

LearningEvent BaseEvent(const std::string& type, uint16_t textId, int pageIndex) {
    (void)pageIndex;
    LearningEvent event;
    event.eventId = LearningSync_NewEventId();
    event.type = type;
    event.occurredAt = LearningSync_CurrentTimestamp();
    event.gameId = kGameId;
    event.adapterId = kAdapterId;
    event.contentVersion =
        StudyRepository_IsCorpusLoaded() ? StudyRepository_GetCorpusVersion() : "corpus-unavailable";
    event.messageId = MessageId(textId);
    return event;
}

} // namespace

void LearningSync_Initialize() {
    if (sClient != nullptr) {
        LearningSync_Configure();
        return;
    }
    sClient = std::make_unique<LearningSyncClient>(
        Ship::Context::GetPathRelativeToAppDirectory("jp_assist_sync.json"),
        LearningSync_CreateDefaultHttpTransport(), LearningClientIdentity{ "Ship of Harkinian", kGameId, kAdapterId });
    sClient->Start();
    LearningSync_Configure();
}

void LearningSync_Configure() {
    if (sClient == nullptr) {
        return;
    }
    sClient->Configure(CVarGetInteger(CVAR_ENHANCEMENT("JPAssist.AccountSync.Enabled"), 0) != 0,
                       CVarGetString(CVAR_ENHANCEMENT("JPAssist.AccountSync.Endpoint"), kDefaultEndpoint));
}

void LearningSync_BeginPairing() {
    if (sClient != nullptr) {
        sClient->BeginPairing();
    }
}

void LearningSync_Disconnect() {
    if (sClient != nullptr) {
        sClient->Disconnect();
    }
}

LearningSyncStatus LearningSync_GetStatus() {
    return sClient != nullptr ? sClient->GetStatus() : LearningSyncStatus{};
}

void LearningSync_RecordWordEvent(const std::string& type, const StudyToken& token, uint16_t textId,
                                  int pageIndex, uint32_t count) {
    if (sClient == nullptr) {
        return;
    }
    LearningEvent event = BaseEvent(type, textId, pageIndex);
    event.wordId = token.Id();
    event.senseId = token.senseId;
    event.count = count;
    sClient->Enqueue(event);
}

void LearningSync_RecordDialogueEvent(const std::string& type, uint16_t textId, int pageIndex) {
    if (sClient != nullptr) {
        LearningEvent event = BaseEvent(type, textId, pageIndex);
        sClient->Enqueue(event);
    }
}

} // namespace JPAssist
