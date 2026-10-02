#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "LearningSyncStore.h"

namespace JPAssist {

struct LearningHttpResponse {
    int statusCode = 0;
    std::string body;
    std::string error;
};

class LearningHttpTransport {
  public:
    virtual ~LearningHttpTransport() = default;
    virtual bool IsAvailable() const = 0;
    virtual LearningHttpResponse Post(const std::string& url, const std::string& body,
                                      const std::string& bearerToken = "") = 0;
};

struct LearningSyncStatus {
    bool enabled = false;
    bool transportAvailable = false;
    bool pairing = false;
    bool connected = false;
    size_t pendingEventCount = 0;
    std::string userCode;
    std::string verificationUrl;
    std::string message;
};

struct LearningClientIdentity {
    std::string deviceName;
    std::string gameId;
    std::string adapterId;
};

std::unique_ptr<LearningHttpTransport> LearningSync_CreateDefaultHttpTransport();

// Transport-independent account client. Network calls run only on its worker
// thread; producers synchronously append to the durable outbox and return.
class LearningSyncClient {
  public:
    LearningSyncClient(std::string storePath, std::unique_ptr<LearningHttpTransport> transport,
                       LearningClientIdentity identity);
    ~LearningSyncClient();

    bool Start();
    void Stop();
    void Configure(bool enabled, std::string endpoint);
    void BeginPairing();
    void Disconnect();
    bool Enqueue(const LearningEvent& event);
    LearningSyncStatus GetStatus() const;

  private:
    void WorkerLoop();
    bool RequestPairing(const std::string& endpoint);
    bool PollPairing(const std::string& endpoint, const LearningPairing& pairing);
    bool UploadBatch(const std::string& endpoint, const LearningDevice& device);
    void SetMessage(std::string message);
    static std::string NormalizeEndpoint(std::string endpoint);

    LearningSyncStore mStore;
    std::unique_ptr<LearningHttpTransport> mTransport;
    LearningClientIdentity mIdentity;
    mutable std::mutex mMutex;
    std::condition_variable mCondition;
    std::thread mWorker;
    bool mStarted = false;
    bool mStopRequested = false;
    bool mWakeRequested = false;
    bool mEnabled = false;
    bool mBeginPairingRequested = false;
    std::string mEndpoint;
    std::string mMessage;
};

std::string LearningSync_NewEventId();
std::string LearningSync_CurrentTimestamp();

} // namespace JPAssist
