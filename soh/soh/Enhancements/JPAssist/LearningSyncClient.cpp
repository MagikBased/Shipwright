#include "LearningSyncClient.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#if defined(JPASSIST_HAS_CURL)
#include <curl/curl.h>
#endif

namespace JPAssist {
namespace {

constexpr size_t kMaximumBatchSize = 250;

std::string JoinUrl(const std::string& endpoint, const char* path) {
    return endpoint + path;
}

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

std::string ErrorFromResponse(const LearningHttpResponse& response) {
    if (!response.error.empty()) {
        return response.error;
    }
    try {
        const nlohmann::json json = nlohmann::json::parse(response.body);
        if (json.contains("error")) {
            return json["error"].value("message", "request failed");
        }
    } catch (...) {
    }
    return "HTTP " + std::to_string(response.statusCode);
}

#if defined(JPASSIST_HAS_CURL)
class CurlLearningHttpTransport final : public LearningHttpTransport {
  public:
    CurlLearningHttpTransport() {
        static std::once_flag initializeOnce;
        std::call_once(initializeOnce, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    bool IsAvailable() const override {
        return true;
    }

    LearningHttpResponse Post(const std::string& url, const std::string& body,
                              const std::string& bearerToken) override {
        LearningHttpResponse response;
        CURL* handle = curl_easy_init();
        if (handle == nullptr) {
            response.error = "could not initialize libcurl";
            return response;
        }

        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (!bearerToken.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Bearer " + bearerToken).c_str());
        }
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "JPAssist-LearningClient/0.1");
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t count, void* output) {
            static_cast<std::string*>(output)->append(data, size * count);
            return size * count;
        });
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);

        const CURLcode result = curl_easy_perform(handle);
        if (result != CURLE_OK) {
            response.error = curl_easy_strerror(result);
        } else {
            long statusCode = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &statusCode);
            response.statusCode = static_cast<int>(statusCode);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
        return response;
    }
};
#else
class UnavailableLearningHttpTransport final : public LearningHttpTransport {
  public:
    bool IsAvailable() const override {
        return false;
    }

    LearningHttpResponse Post(const std::string&, const std::string&, const std::string&) override {
        return { 0, "", "this build does not include HTTPS support" };
    }
};
#endif

} // namespace

std::unique_ptr<LearningHttpTransport> LearningSync_CreateDefaultHttpTransport() {
#if defined(JPASSIST_HAS_CURL)
    return std::make_unique<CurlLearningHttpTransport>();
#else
    return std::make_unique<UnavailableLearningHttpTransport>();
#endif
}

LearningSyncClient::LearningSyncClient(std::string storePath, std::unique_ptr<LearningHttpTransport> transport,
                                       LearningClientIdentity identity)
    : mStore(std::move(storePath)), mTransport(std::move(transport)), mIdentity(std::move(identity)) {
}

LearningSyncClient::~LearningSyncClient() {
    Stop();
}

bool LearningSyncClient::Start() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mStarted) {
        return true;
    }
    if (!mStore.Load()) {
        mMessage = "Could not load account-sync state: " + mStore.LastError();
    }
    mStopRequested = false;
    mStarted = true;
    mWorker = std::thread(&LearningSyncClient::WorkerLoop, this);
    return true;
}

void LearningSyncClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mStarted) {
            return;
        }
        mStopRequested = true;
    }
    mCondition.notify_all();
    if (mWorker.joinable()) {
        mWorker.join();
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mStarted = false;
}

void LearningSyncClient::Configure(bool enabled, std::string endpoint) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mEnabled = enabled;
        mEndpoint = NormalizeEndpoint(std::move(endpoint));
        mWakeRequested = true;
        if (!enabled) {
            mMessage = "Account sync is disabled";
        }
    }
    mCondition.notify_all();
}

void LearningSyncClient::BeginPairing() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mBeginPairingRequested = true;
        mWakeRequested = true;
        mMessage = "Requesting a pairing code...";
    }
    mCondition.notify_all();
}

void LearningSyncClient::Disconnect() {
    mStore.Disconnect();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mBeginPairingRequested = false;
        mWakeRequested = true;
        mMessage = "This device is disconnected";
    }
    mCondition.notify_all();
}

bool LearningSyncClient::Enqueue(const LearningEvent& event) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mEnabled) {
            return false;
        }
    }
    const bool stored = mStore.Enqueue(event);
    if (stored) {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mWakeRequested = true;
        }
        mCondition.notify_all();
    }
    return stored;
}

LearningSyncStatus LearningSyncClient::GetStatus() const {
    const LearningSyncSnapshot snapshot = mStore.Snapshot();
    std::lock_guard<std::mutex> lock(mMutex);
    LearningSyncStatus status;
    status.enabled = mEnabled;
    status.transportAvailable = mTransport != nullptr && mTransport->IsAvailable();
    status.pairing = !snapshot.pairing.deviceCode.empty();
    status.connected = !snapshot.device.deviceToken.empty();
    status.pendingEventCount = snapshot.pendingEvents.size();
    status.userCode = snapshot.pairing.userCode;
    status.verificationUrl = snapshot.pairing.verificationPath.empty()
                                 ? ""
                                 : snapshot.pairing.serviceEndpoint + snapshot.pairing.verificationPath;
    status.message = mMessage;
    return status;
}

void LearningSyncClient::WorkerLoop() {
    uint32_t retrySeconds = 1;
    while (true) {
        bool enabled = false;
        bool beginPairing = false;
        std::string endpoint;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCondition.wait_for(lock, std::chrono::seconds(retrySeconds), [this]() {
                return mStopRequested || mBeginPairingRequested || mWakeRequested;
            });
            if (mStopRequested) {
                return;
            }
            enabled = mEnabled;
            endpoint = mEndpoint;
            beginPairing = mBeginPairingRequested;
            mBeginPairingRequested = false;
            mWakeRequested = false;
        }

        if (!enabled) {
            retrySeconds = 3;
            continue;
        }
        if (endpoint.empty()) {
            SetMessage("Use an HTTPS service URL (HTTP is allowed only on localhost)");
            retrySeconds = 10;
            continue;
        }
        if (mTransport == nullptr || !mTransport->IsAvailable()) {
            SetMessage("This build does not include HTTPS support");
            retrySeconds = 30;
            continue;
        }

        bool succeeded = true;
        uint32_t successDelaySeconds = 3;
        LearningSyncSnapshot snapshot = mStore.Snapshot();
        if (beginPairing) {
            succeeded = RequestPairing(endpoint);
        } else if (!snapshot.pairing.deviceCode.empty()) {
            if (snapshot.pairing.serviceEndpoint != endpoint) {
                SetMessage("Service URL changed; disconnect and request a new pairing code");
                successDelaySeconds = 10;
            } else {
                succeeded = PollPairing(endpoint, snapshot.pairing);
                successDelaySeconds = std::max(snapshot.pairing.pollIntervalSeconds, 1U);
            }
        } else if (!snapshot.device.deviceToken.empty() && !snapshot.pendingEvents.empty()) {
            if (snapshot.device.serviceEndpoint != endpoint) {
                SetMessage("Service URL changed; disconnect and pair this device again");
                successDelaySeconds = 10;
            } else {
                succeeded = UploadBatch(endpoint, snapshot.device);
            }
        }

        if (succeeded) {
            retrySeconds = successDelaySeconds;
        } else {
            retrySeconds = std::min(std::max(retrySeconds * 2, 3U), 60U);
        }
    }
}

bool LearningSyncClient::RequestPairing(const std::string& endpoint) {
    const nlohmann::json request = { { "deviceName", mIdentity.deviceName },
                                     { "adapterId", mIdentity.adapterId },
                                     { "gameId", mIdentity.gameId } };
    const LearningHttpResponse response =
        mTransport->Post(JoinUrl(endpoint, "/v1/device-pairings"), request.dump());
    if (response.statusCode != 201) {
        SetMessage("Could not start pairing: " + ErrorFromResponse(response));
        return false;
    }
    try {
        const nlohmann::json json = nlohmann::json::parse(response.body);
        LearningPairing pairing;
        pairing.deviceCode = json.at("deviceCode").get<std::string>();
        pairing.userCode = json.at("userCode").get<std::string>();
        pairing.verificationPath = json.value("verificationPath", "/#pair-device");
        pairing.pollIntervalSeconds = json.value("pollInterval", 3U);
        pairing.expiresAtUnix = static_cast<int64_t>(std::time(nullptr)) + json.value("expiresIn", 600);
        pairing.serviceEndpoint = endpoint;
        if (!mStore.SetPairing(pairing)) {
            SetMessage("Could not save pairing state: " + mStore.LastError());
            return false;
        }
        SetMessage("Approve the displayed code in your browser");
        return true;
    } catch (const std::exception& error) {
        SetMessage("Invalid pairing response: " + std::string(error.what()));
        return false;
    }
}

bool LearningSyncClient::PollPairing(const std::string& endpoint, const LearningPairing& pairing) {
    if (pairing.expiresAtUnix <= static_cast<int64_t>(std::time(nullptr))) {
        mStore.ClearPairing();
        SetMessage("Pairing code expired; request a new one");
        return true;
    }
    const nlohmann::json request = { { "deviceCode", pairing.deviceCode } };
    const LearningHttpResponse response =
        mTransport->Post(JoinUrl(endpoint, "/v1/device-pairings/token"), request.dump());
    if (response.statusCode == 428) {
        return true;
    }
    if (response.statusCode == 410) {
        mStore.ClearPairing();
        SetMessage("Pairing code expired; request a new one");
        return true;
    }
    if (response.statusCode != 200) {
        SetMessage("Could not finish pairing: " + ErrorFromResponse(response));
        return false;
    }
    try {
        const nlohmann::json json = nlohmann::json::parse(response.body);
        LearningDevice device;
        device.deviceId = json.at("deviceId").get<std::string>();
        device.deviceToken = json.at("deviceToken").get<std::string>();
        device.gameId = json.at("gameId").get<std::string>();
        device.adapterId = json.at("adapterId").get<std::string>();
        device.serviceEndpoint = endpoint;
        if (!mStore.SetDevice(device)) {
            SetMessage("Could not save device credentials: " + mStore.LastError());
            return false;
        }
        SetMessage("Connected to learning account");
        return true;
    } catch (const std::exception& error) {
        SetMessage("Invalid device response: " + std::string(error.what()));
        return false;
    }
}

bool LearningSyncClient::UploadBatch(const std::string& endpoint, const LearningDevice& device) {
    const std::vector<LearningEvent> events = mStore.PendingBatch(kMaximumBatchSize);
    if (events.empty()) {
        return true;
    }
    nlohmann::json request;
    request["events"] = nlohmann::json::array();
    for (const auto& event : events) {
        request["events"].push_back(EventToJson(event));
    }
    const LearningHttpResponse response = mTransport->Post(JoinUrl(endpoint, "/v1/events/batch"), request.dump(),
                                                           device.deviceToken);
    if (response.statusCode == 401) {
        SetMessage("Device access was revoked; disconnect and pair again");
        return false;
    }
    if (response.statusCode != 200) {
        SetMessage("Could not upload learning events: " + ErrorFromResponse(response));
        return false;
    }
    try {
        const nlohmann::json json = nlohmann::json::parse(response.body);
        std::vector<std::string> acknowledged = json.value("acceptedEventIds", std::vector<std::string>{});
        const std::vector<std::string> duplicates =
            json.value("duplicateEventIds", std::vector<std::string>{});
        acknowledged.insert(acknowledged.end(), duplicates.begin(), duplicates.end());
        if (!mStore.Acknowledge(acknowledged)) {
            SetMessage("Events uploaded but acknowledgement could not be saved; retry is safe");
            return false;
        }
        SetMessage("Learning progress synchronized");
        return true;
    } catch (const std::exception& error) {
        SetMessage("Invalid event-upload response: " + std::string(error.what()));
        return false;
    }
}

void LearningSyncClient::SetMessage(std::string message) {
    std::lock_guard<std::mutex> lock(mMutex);
    mMessage = std::move(message);
}

std::string LearningSyncClient::NormalizeEndpoint(std::string endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    if (endpoint.starts_with("https://")) {
        return endpoint;
    }
    // Permit cleartext only for a service on the same machine. Device bearer
    // tokens must not be sent to a remote HTTP endpoint.
    constexpr const char* loopbackHosts[] = { "http://127.0.0.1", "http://localhost", "http://[::1]" };
    for (const char* prefix : loopbackHosts) {
        const size_t prefixLength = std::char_traits<char>::length(prefix);
        if (endpoint.starts_with(prefix) &&
            (endpoint.size() == prefixLength || endpoint[prefixLength] == ':' || endpoint[prefixLength] == '/')) {
            return endpoint;
        }
    }
    return "";
}

std::string LearningSync_NewEventId() {
    static std::mutex randomMutex;
    static std::mt19937_64 random(std::random_device{}());
    std::lock_guard<std::mutex> lock(randomMutex);
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) << '-'
           << std::setw(16) << random();
    return output.str();
}

std::string LearningSync_CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << milliseconds
           << 'Z';
    return output.str();
}

} // namespace JPAssist
