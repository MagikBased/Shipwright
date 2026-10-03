#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "LearningSyncClient.h"

namespace {

JPAssist::LearningClientIdentity TestIdentity() {
    return { "Ship of Harkinian", "ocarina-of-time", "ship-of-harkinian" };
}

struct RecordedRequest {
    std::string url;
    std::string body;
    std::string bearerToken;
};

class FakeTransport final : public JPAssist::LearningHttpTransport {
  public:
    bool IsAvailable() const override {
        return true;
    }

    JPAssist::LearningHttpResponse Post(const std::string& url, const std::string& body,
                                        const std::string& bearerToken) override {
        std::lock_guard<std::mutex> lock(mutex);
        requests.push_back({ url, body, bearerToken });
        JPAssist::LearningHttpResponse result = responses.empty()
                                                      ? JPAssist::LearningHttpResponse{ 500, "", "no fake response" }
                                                      : responses.front();
        if (!responses.empty()) {
            responses.pop_front();
        }
        condition.notify_all();
        return result;
    }

    void AddResponse(JPAssist::LearningHttpResponse response) {
        std::lock_guard<std::mutex> lock(mutex);
        responses.push_back(std::move(response));
    }

    bool WaitForRequests(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(3), [this, count]() { return requests.size() >= count; });
    }

    RecordedRequest Request(size_t index) {
        std::lock_guard<std::mutex> lock(mutex);
        return requests.at(index);
    }

    size_t RequestCount() {
        std::lock_guard<std::mutex> lock(mutex);
        return requests.size();
    }

  private:
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<JPAssist::LearningHttpResponse> responses;
    std::vector<RecordedRequest> requests;
};

class ClientStorePath {
  public:
    ClientStorePath() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("jp-assist-client-" + std::to_string(suffix) + ".json");
    }
    ~ClientStorePath() {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.string() + ".tmp", error);
    }
    std::filesystem::path path;
};

TEST(LearningSyncClient, RequestsPairingWithoutAccountCredentials) {
    ClientStorePath temporary;
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    fake->AddResponse({ 201,
                        R"({"deviceCode":"secret","userCode":"ABCD-2345","verificationPath":"/#pair-device","expiresIn":600,"pollInterval":3})",
                        "" });
    JPAssist::LearningSyncClient client(temporary.path.string(), std::move(transport), TestIdentity());
    ASSERT_TRUE(client.Start());
    client.Configure(true, "https://learn.example.test/");
    client.BeginPairing();
    ASSERT_TRUE(fake->WaitForRequests(1));

    const RecordedRequest request = fake->Request(0);
    EXPECT_EQ(request.url, "https://learn.example.test/v1/device-pairings");
    EXPECT_TRUE(request.bearerToken.empty());
    const auto body = nlohmann::json::parse(request.body);
    EXPECT_EQ(body["adapterId"], "ship-of-harkinian");
    EXPECT_EQ(body["gameId"], "ocarina-of-time");

    for (int tries = 0; tries < 50 && !client.GetStatus().pairing; tries++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto status = client.GetStatus();
    EXPECT_TRUE(status.pairing);
    EXPECT_EQ(status.userCode, "ABCD-2345");
    EXPECT_EQ(status.verificationUrl, "https://learn.example.test/#pair-device");
    client.Stop();
}

TEST(LearningSyncClient, UploadsDurableEventsAndAcknowledgesServerDuplicates) {
    ClientStorePath temporary;
    JPAssist::LearningSyncStore seed(temporary.path.string());
    ASSERT_TRUE(seed.Load());
    ASSERT_TRUE(seed.SetDevice(
        { "device-id", "device-token", "ocarina-of-time", "ship-of-harkinian", "https://learn.example.test" }));
    JPAssist::LearningEvent event;
    event.eventId = "event-1";
    event.type = "word_saved";
    event.occurredAt = "2026-10-02T12:00:00.000Z";
    event.gameId = "ocarina-of-time";
    event.adapterId = "ship-of-harkinian";
    event.contentVersion = "n64-ntsc-1.2-v1";
    event.wordId = "武器|ぶき";
    event.pageIndex = 2;
    ASSERT_TRUE(seed.Enqueue(event));

    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    fake->AddResponse({ 200, R"({"acceptedEventIds":[],"duplicateEventIds":["event-1"]})", "" });
    JPAssist::LearningSyncClient client(temporary.path.string(), std::move(transport), TestIdentity());
    ASSERT_TRUE(client.Start());
    client.Configure(true, "https://learn.example.test");
    ASSERT_TRUE(fake->WaitForRequests(1));

    const RecordedRequest request = fake->Request(0);
    EXPECT_EQ(request.url, "https://learn.example.test/v1/events/batch");
    EXPECT_EQ(request.bearerToken, "device-token");
    const auto body = nlohmann::json::parse(request.body);
    ASSERT_EQ(body["events"].size(), 1);
    EXPECT_EQ(body["events"][0]["wordId"], "武器|ぶき");
    EXPECT_EQ(body["events"][0]["pageIndex"], 2);
    EXPECT_FALSE(body["events"][0].contains("japaneseText"));

    for (int tries = 0; tries < 50 && client.GetStatus().pendingEventCount != 0; tries++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(client.GetStatus().pendingEventCount, 0);
    client.Stop();
}

TEST(LearningSyncClient, RejectsNonHttpServiceUrlsBeforeTransport) {
    ClientStorePath temporary;
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    JPAssist::LearningSyncClient client(temporary.path.string(), std::move(transport), TestIdentity());
    ASSERT_TRUE(client.Start());
    client.Configure(true, "file:///tmp/not-a-service");
    client.BeginPairing();

    for (int tries = 0; tries < 50 &&
                        client.GetStatus().message != "Use an HTTPS service URL (HTTP is allowed only on localhost)";
         tries++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(fake->RequestCount(), 0);
    EXPECT_EQ(client.GetStatus().message, "Use an HTTPS service URL (HTTP is allowed only on localhost)");
    client.Stop();
}

TEST(LearningSyncClient, RejectsRemoteCleartextServiceUrlsBeforeTransport) {
    ClientStorePath temporary;
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    JPAssist::LearningSyncClient client(temporary.path.string(), std::move(transport), TestIdentity());
    ASSERT_TRUE(client.Start());
    client.Configure(true, "http://learn.example.test");
    client.BeginPairing();

    for (int tries = 0; tries < 50 &&
                        client.GetStatus().message != "Use an HTTPS service URL (HTTP is allowed only on localhost)";
         tries++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(fake->RequestCount(), 0);
    EXPECT_EQ(client.GetStatus().message, "Use an HTTPS service URL (HTTP is allowed only on localhost)");
    client.Stop();
}

TEST(LearningSyncClient, DoesNotSendDeviceTokenAfterServiceUrlChanges) {
    ClientStorePath temporary;
    JPAssist::LearningSyncStore seed(temporary.path.string());
    ASSERT_TRUE(seed.Load());
    ASSERT_TRUE(seed.SetDevice(
        { "device-id", "device-token", "ocarina-of-time", "ship-of-harkinian", "https://old.example.test" }));
    JPAssist::LearningEvent event;
    event.eventId = "event-endpoint-change";
    event.type = "word_saved";
    event.occurredAt = "2026-10-02T12:00:00.000Z";
    event.gameId = "ocarina-of-time";
    event.adapterId = "ship-of-harkinian";
    event.contentVersion = "corpus-v1";
    event.wordId = "森|もり";
    ASSERT_TRUE(seed.Enqueue(event));

    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    JPAssist::LearningSyncClient client(temporary.path.string(), std::move(transport), TestIdentity());
    ASSERT_TRUE(client.Start());
    client.Configure(true, "https://new.example.test");
    for (int tries = 0; tries < 50 &&
                        client.GetStatus().message != "Service URL changed; disconnect and pair this device again";
         tries++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(fake->RequestCount(), 0);
    EXPECT_EQ(client.GetStatus().pendingEventCount, 1);
    EXPECT_EQ(client.GetStatus().message, "Service URL changed; disconnect and pair this device again");
    client.Stop();
}

} // namespace
