#include "PluginSync.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace JPAssistPlugin {

namespace {

std::string NewEventId() {
    static std::mt19937_64 random(std::random_device{}());
    std::ostringstream value;
    value << std::hex << std::chrono::steady_clock::now().time_since_epoch().count() << '-' << random();
    return value.str();
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream value;
    value << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return value.str();
}

std::string MessageId(uint64_t dialogueId) {
    std::ostringstream value;
    value << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << dialogueId;
    return value.str();
}

bool SameStatus(const JPAssist::LearningSyncStatus& left, const JPAssist::LearningSyncStatus& right) {
    return left.enabled == right.enabled && left.transportAvailable == right.transportAvailable &&
           left.pairing == right.pairing && left.connected == right.connected &&
           left.pendingEventCount == right.pendingEventCount && left.userCode == right.userCode &&
           left.verificationUrl == right.verificationUrl && left.message == right.message;
}

} // namespace

bool SyncOutbox::Initialize(const StudyModHostApi& host, const char* modId, std::string contentVersion) {
    mHost = &host;
    mModId = modId == nullptr ? "" : modId;
    mHostId = host.host_id == nullptr ? "unknown-host" : host.host_id;
    mGameId = host.game_id == nullptr ? "unknown-game" : host.game_id;
    mContentVersion = std::move(contentVersion);
    if (mModId.empty() || host.get_writable_path == nullptr) {
        return false;
    }
    const size_t size = host.get_writable_path(mModId.c_str(), "jp_assist_sync.json", nullptr, 0);
    if (size == 0) {
        return false;
    }
    std::string path(size, '\0');
    if (host.get_writable_path(mModId.c_str(), "jp_assist_sync.json", path.data(), path.size()) != size) {
        return false;
    }
    path.resize(size - 1);
    mPath = std::move(path);
    return Reload();
}

bool SyncOutbox::Reload() {
    if (mPath.empty()) {
        return false;
    }
    Shutdown();
    mClient = std::make_unique<JPAssist::LearningSyncClient>(
        mPath, JPAssist::LearningSync_CreateDefaultHttpTransport(),
        JPAssist::LearningClientIdentity{ "JP Assist plugin", mGameId, mHostId });
    const bool loaded = mClient->Start();
    mHasPublishedStatus = false;
    RefreshEnabled();
    return loaded;
}

void SyncOutbox::Shutdown() {
    if (mClient != nullptr) {
        mClient->Stop();
        mClient.reset();
    }
}

void SyncOutbox::RefreshEnabled() {
    mEnabled = mHost != nullptr && mHost->get_int_setting != nullptr &&
               mHost->get_int_setting(mModId.c_str(), "AccountSyncEnabled", 0) != 0;
    std::string endpoint = "http://127.0.0.1:8766";
    if (mHost != nullptr && mHost->get_string_setting != nullptr) {
        const size_t size = mHost->get_string_setting(mModId.c_str(), "AccountSyncEndpoint", endpoint.c_str(),
                                                       nullptr, 0);
        if (size != 0) {
            std::string configured(size, '\0');
            mHost->get_string_setting(mModId.c_str(), "AccountSyncEndpoint", endpoint.c_str(), configured.data(),
                                      configured.size());
            configured.resize(size - 1);
            endpoint = std::move(configured);
        }
    }
    if (mClient != nullptr && (endpoint != mEndpoint || mClient->GetStatus().enabled != mEnabled)) {
        mEndpoint = std::move(endpoint);
        mClient->Configure(mEnabled, mEndpoint);
    }
    if (mClient != nullptr && mHost != nullptr && mHost->get_int_setting != nullptr &&
        mHost->set_int_setting != nullptr) {
        if (mHost->get_int_setting(mModId.c_str(), "BeginPairing", 0) != 0) {
            mHost->set_int_setting(mModId.c_str(), "BeginPairing", 0);
            mClient->BeginPairing();
        }
        if (mHost->get_int_setting(mModId.c_str(), "Disconnect", 0) != 0) {
            mHost->set_int_setting(mModId.c_str(), "Disconnect", 0);
            mClient->Disconnect();
        }
    }
    if (mClient != nullptr && mHost != nullptr) {
        const auto status = mClient->GetStatus();
        if (mHasPublishedStatus && SameStatus(status, mPublishedStatus)) {
            return;
        }
        if (mHost->set_int_setting != nullptr) {
            mHost->set_int_setting(mModId.c_str(), "AccountConnected", status.connected ? 1 : 0);
            mHost->set_int_setting(mModId.c_str(), "AccountPairing", status.pairing ? 1 : 0);
            mHost->set_int_setting(mModId.c_str(), "AccountTransportAvailable",
                                   status.transportAvailable ? 1 : 0);
            mHost->set_int_setting(mModId.c_str(), "AccountPendingEvents",
                                   static_cast<int32_t>(status.pendingEventCount));
        }
        if (mHost->set_string_setting != nullptr) {
            mHost->set_string_setting(mModId.c_str(), "AccountUserCode", status.userCode.c_str());
            mHost->set_string_setting(mModId.c_str(), "AccountVerificationUrl", status.verificationUrl.c_str());
            mHost->set_string_setting(mModId.c_str(), "AccountStatusMessage", status.message.c_str());
        }
        mPublishedStatus = status;
        mHasPublishedStatus = true;
    }
}

JPAssist::LearningEvent SyncOutbox::BaseEvent(const std::string& type, uint64_t dialogueId,
                                               uint32_t pageIndex) const {
    JPAssist::LearningEvent event;
    event.eventId = NewEventId();
    event.type = type;
    event.occurredAt = Timestamp();
    event.gameId = mGameId;
    event.adapterId = mHostId;
    event.contentVersion = mContentVersion.empty() ? "unavailable" : mContentVersion;
    event.messageId = MessageId(dialogueId);
    event.pageIndex = static_cast<int32_t>(pageIndex);
    return event;
}

bool SyncOutbox::RecordDialogue(const std::string& type, uint64_t dialogueId, uint32_t pageIndex) {
    RefreshEnabled();
    return mEnabled && mClient != nullptr && mClient->Enqueue(BaseEvent(type, dialogueId, pageIndex));
}

bool SyncOutbox::RecordWord(const std::string& type, const Token& token, uint64_t dialogueId,
                            uint32_t pageIndex, uint32_t count) {
    RefreshEnabled();
    if (!mEnabled || mClient == nullptr) {
        return false;
    }
    auto event = BaseEvent(type, dialogueId, pageIndex);
    event.wordId = token.Id();
    event.senseId = token.senseId;
    event.count = count;
    return mClient->Enqueue(event);
}

size_t SyncOutbox::PendingCount() const {
    return mClient == nullptr ? 0 : mClient->GetStatus().pendingEventCount;
}

} // namespace JPAssistPlugin
