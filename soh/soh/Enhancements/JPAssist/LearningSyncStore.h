#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace JPAssist {

struct LearningEvent {
    std::string eventId;
    std::string type;
    std::string occurredAt;
    std::string gameId;
    std::string adapterId;
    std::string contentVersion;
    std::string wordId;
    std::string senseId;
    std::string messageId;
    std::string locationId;
    int32_t pageIndex = -1;
    uint32_t count = 1;
};

struct LearningPairing {
    std::string deviceCode;
    std::string userCode;
    std::string verificationPath;
    int64_t expiresAtUnix = 0;
    uint32_t pollIntervalSeconds = 3;
    std::string serviceEndpoint;
};

struct LearningDevice {
    std::string deviceId;
    std::string deviceToken;
    std::string gameId;
    std::string adapterId;
    std::string serviceEndpoint;
};

struct LearningSyncSnapshot {
    LearningPairing pairing;
    LearningDevice device;
    std::vector<LearningEvent> pendingEvents;
};

// Durable, game-neutral state for account synchronization. Every mutation is
// written atomically so an interrupted game session may safely replay its
// unacknowledged event IDs on the next launch.
class LearningSyncStore {
  public:
    explicit LearningSyncStore(std::string path);

    bool Load();
    bool Enqueue(const LearningEvent& event);
    std::vector<LearningEvent> PendingBatch(size_t limit) const;
    bool Acknowledge(const std::vector<std::string>& eventIds);

    bool SetPairing(const LearningPairing& pairing);
    bool SetDevice(const LearningDevice& device);
    bool ClearPairing();
    bool Disconnect();

    LearningSyncSnapshot Snapshot() const;
    std::string LastError() const;

  private:
    bool SaveLocked();

    std::string mPath;
    mutable std::mutex mMutex;
    LearningPairing mPairing;
    LearningDevice mDevice;
    std::vector<LearningEvent> mPendingEvents;
    std::string mLastError;
};

} // namespace JPAssist
