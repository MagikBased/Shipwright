#include "LearningSyncStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace JPAssist {
namespace {

constexpr int kSchemaVersion = 1;

nlohmann::json EventToJson(const LearningEvent& event) {
    nlohmann::json json = {
        { "eventId", event.eventId },
        { "type", event.type },
        { "occurredAt", event.occurredAt },
        { "gameId", event.gameId },
        { "adapterId", event.adapterId },
        { "contentVersion", event.contentVersion },
        { "count", event.count },
    };
    if (!event.wordId.empty()) {
        json["wordId"] = event.wordId;
    }
    if (!event.senseId.empty()) {
        json["senseId"] = event.senseId;
    }
    if (!event.messageId.empty()) {
        json["messageId"] = event.messageId;
    }
    if (!event.locationId.empty()) {
        json["locationId"] = event.locationId;
    }
    return json;
}

LearningEvent EventFromJson(const nlohmann::json& json) {
    LearningEvent event;
    event.eventId = json.at("eventId").get<std::string>();
    event.type = json.at("type").get<std::string>();
    event.occurredAt = json.at("occurredAt").get<std::string>();
    event.gameId = json.at("gameId").get<std::string>();
    event.adapterId = json.at("adapterId").get<std::string>();
    event.contentVersion = json.at("contentVersion").get<std::string>();
    event.wordId = json.value("wordId", "");
    event.senseId = json.value("senseId", "");
    event.messageId = json.value("messageId", "");
    event.locationId = json.value("locationId", "");
    event.count = json.value("count", 1U);
    return event;
}

} // namespace

LearningSyncStore::LearningSyncStore(std::string path) : mPath(std::move(path)) {
}

bool LearningSyncStore::Load() {
    std::lock_guard<std::mutex> lock(mMutex);
    mPairing = {};
    mDevice = {};
    mPendingEvents.clear();
    mLastError.clear();

    if (!std::filesystem::exists(mPath)) {
        return true;
    }

    try {
        std::ifstream file(mPath);
        nlohmann::json root;
        file >> root;
        if (root.value("schemaVersion", 0) != kSchemaVersion) {
            mLastError = "unsupported sync-state schema";
            return false;
        }

        const nlohmann::json pairing = root.value("pairing", nlohmann::json::object());
        mPairing.deviceCode = pairing.value("deviceCode", "");
        mPairing.userCode = pairing.value("userCode", "");
        mPairing.verificationPath = pairing.value("verificationPath", "");
        mPairing.expiresAtUnix = pairing.value("expiresAtUnix", static_cast<int64_t>(0));
        mPairing.pollIntervalSeconds = pairing.value("pollIntervalSeconds", 3U);
        mPairing.serviceEndpoint = pairing.value("serviceEndpoint", "");

        const nlohmann::json device = root.value("device", nlohmann::json::object());
        mDevice.deviceId = device.value("deviceId", "");
        mDevice.deviceToken = device.value("deviceToken", "");
        mDevice.gameId = device.value("gameId", "");
        mDevice.adapterId = device.value("adapterId", "");
        mDevice.serviceEndpoint = device.value("serviceEndpoint", "");

        const nlohmann::json pending = root.value("pendingEvents", nlohmann::json::array());
        std::unordered_set<std::string> loadedIds;
        for (const auto& value : pending) {
            LearningEvent event = EventFromJson(value);
            if (!event.eventId.empty() && loadedIds.insert(event.eventId).second) {
                mPendingEvents.push_back(std::move(event));
            }
        }
        return true;
    } catch (const std::exception& error) {
        mPairing = {};
        mDevice = {};
        mPendingEvents.clear();
        mLastError = error.what();
        return false;
    }
}

bool LearningSyncStore::Enqueue(const LearningEvent& event) {
    if (event.eventId.empty() || event.type.empty() || event.occurredAt.empty() || event.gameId.empty() ||
        event.adapterId.empty() || event.contentVersion.empty() || event.count == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    const auto duplicate = std::find_if(mPendingEvents.begin(), mPendingEvents.end(),
                                        [&event](const LearningEvent& value) { return value.eventId == event.eventId; });
    if (duplicate != mPendingEvents.end()) {
        return true;
    }
    mPendingEvents.push_back(event);
    return SaveLocked();
}

std::vector<LearningEvent> LearningSyncStore::PendingBatch(size_t limit) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const size_t count = std::min(limit, mPendingEvents.size());
    return { mPendingEvents.begin(), mPendingEvents.begin() + count };
}

bool LearningSyncStore::Acknowledge(const std::vector<std::string>& eventIds) {
    std::lock_guard<std::mutex> lock(mMutex);
    const std::unordered_set<std::string> acknowledged(eventIds.begin(), eventIds.end());
    std::erase_if(mPendingEvents,
                  [&acknowledged](const LearningEvent& event) { return acknowledged.contains(event.eventId); });
    return SaveLocked();
}

bool LearningSyncStore::SetPairing(const LearningPairing& pairing) {
    std::lock_guard<std::mutex> lock(mMutex);
    mPairing = pairing;
    return SaveLocked();
}

bool LearningSyncStore::SetDevice(const LearningDevice& device) {
    std::lock_guard<std::mutex> lock(mMutex);
    mDevice = device;
    mPairing = {};
    return SaveLocked();
}

bool LearningSyncStore::ClearPairing() {
    std::lock_guard<std::mutex> lock(mMutex);
    mPairing = {};
    return SaveLocked();
}

bool LearningSyncStore::Disconnect() {
    std::lock_guard<std::mutex> lock(mMutex);
    mPairing = {};
    mDevice = {};
    return SaveLocked();
}

LearningSyncSnapshot LearningSyncStore::Snapshot() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return { mPairing, mDevice, mPendingEvents };
}

std::string LearningSyncStore::LastError() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mLastError;
}

bool LearningSyncStore::SaveLocked() {
    try {
        nlohmann::json pending = nlohmann::json::array();
        for (const auto& event : mPendingEvents) {
            pending.push_back(EventToJson(event));
        }
        nlohmann::json root = {
            { "schemaVersion", kSchemaVersion },
            { "pairing",
              {
                  { "deviceCode", mPairing.deviceCode },
                  { "userCode", mPairing.userCode },
                  { "verificationPath", mPairing.verificationPath },
                  { "expiresAtUnix", mPairing.expiresAtUnix },
                  { "pollIntervalSeconds", mPairing.pollIntervalSeconds },
                  { "serviceEndpoint", mPairing.serviceEndpoint },
              } },
            { "device",
              {
                  { "deviceId", mDevice.deviceId },
                  { "deviceToken", mDevice.deviceToken },
                  { "gameId", mDevice.gameId },
                  { "adapterId", mDevice.adapterId },
                  { "serviceEndpoint", mDevice.serviceEndpoint },
              } },
            { "pendingEvents", std::move(pending) },
        };

        const std::filesystem::path path(mPath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        const std::string temporaryPath = mPath + ".tmp";
        {
            std::ofstream file(temporaryPath, std::ios::trunc);
            if (!file.is_open()) {
                mLastError = "could not open sync-state temporary file";
                return false;
            }
            file << root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        std::error_code error;
        std::filesystem::rename(temporaryPath, mPath, error);
        if (error) {
            // Windows does not replace an existing destination atomically.
            std::filesystem::remove(mPath, error);
            error.clear();
            std::filesystem::rename(temporaryPath, mPath, error);
        }
        if (error) {
            mLastError = error.message();
            return false;
        }
#if !defined(_WIN32)
        // The device bearer token grants write access to the linked account.
        // Keep the local state private even when the user's default umask is
        // unusually permissive.
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, error);
        if (error) {
            mLastError = "could not restrict sync-state permissions: " + error.message();
            return false;
        }
#endif
        mLastError.clear();
        return true;
    } catch (const std::exception& error) {
        mLastError = error.what();
        return false;
    }
}

} // namespace JPAssist
