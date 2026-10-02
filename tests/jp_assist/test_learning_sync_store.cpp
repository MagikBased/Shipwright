#include <chrono>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "LearningSyncStore.h"

namespace {

class TemporaryStore {
  public:
    TemporaryStore() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("jp-assist-sync-" + std::to_string(suffix) + ".json");
    }

    ~TemporaryStore() {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.string() + ".tmp", error);
    }

    std::filesystem::path path;
};

JPAssist::LearningEvent MakeEvent(std::string id, std::string type = "word_encountered") {
    JPAssist::LearningEvent event;
    event.eventId = std::move(id);
    event.type = std::move(type);
    event.occurredAt = "2026-10-02T12:00:00.000Z";
    event.gameId = "ocarina-of-time";
    event.adapterId = "ship-of-harkinian";
    event.contentVersion = "n64-ntsc-1.2-v1";
    event.wordId = "武器|ぶき";
    return event;
}

TEST(LearningSyncStore, PersistsUnacknowledgedEventsAcrossRestart) {
    TemporaryStore temporary;
    {
        JPAssist::LearningSyncStore store(temporary.path.string());
        ASSERT_TRUE(store.Load());
        ASSERT_TRUE(store.Enqueue(MakeEvent("event-1")));
        ASSERT_TRUE(store.Enqueue(MakeEvent("event-2", "word_saved")));
    }

    JPAssist::LearningSyncStore reloaded(temporary.path.string());
    ASSERT_TRUE(reloaded.Load()) << reloaded.LastError();
    const auto pending = reloaded.PendingBatch(250);
    ASSERT_EQ(pending.size(), 2);
    EXPECT_EQ(pending[0].eventId, "event-1");
    EXPECT_EQ(pending[0].wordId, "武器|ぶき");
    EXPECT_EQ(pending[1].type, "word_saved");
}

TEST(LearningSyncStore, DuplicateEnqueueAndAcknowledgementAreIdempotent) {
    TemporaryStore temporary;
    JPAssist::LearningSyncStore store(temporary.path.string());
    ASSERT_TRUE(store.Load());
    ASSERT_TRUE(store.Enqueue(MakeEvent("event-1")));
    ASSERT_TRUE(store.Enqueue(MakeEvent("event-1")));
    ASSERT_EQ(store.PendingBatch(250).size(), 1);

    ASSERT_TRUE(store.Acknowledge({ "event-1", "event-1", "not-present" }));
    EXPECT_TRUE(store.PendingBatch(250).empty());
    ASSERT_TRUE(store.Acknowledge({ "event-1" }));
}

TEST(LearningSyncStore, PersistsPairingAndReplacesItWithDeviceCredentials) {
    TemporaryStore temporary;
    JPAssist::LearningSyncStore store(temporary.path.string());
    ASSERT_TRUE(store.Load());
    ASSERT_TRUE(store.SetPairing(
        { "device-secret", "ABCD-2345", "/#pair-device", 12345, 3, "https://learn.example.test" }));

    JPAssist::LearningSyncStore pairingReload(temporary.path.string());
    ASSERT_TRUE(pairingReload.Load());
    EXPECT_EQ(pairingReload.Snapshot().pairing.userCode, "ABCD-2345");

    ASSERT_TRUE(pairingReload.SetDevice(
        { "device-id", "device-token", "ocarina-of-time", "ship-of-harkinian", "https://learn.example.test" }));
    JPAssist::LearningSyncStore deviceReload(temporary.path.string());
    ASSERT_TRUE(deviceReload.Load());
    const auto snapshot = deviceReload.Snapshot();
    EXPECT_TRUE(snapshot.pairing.deviceCode.empty());
    EXPECT_EQ(snapshot.device.deviceToken, "device-token");
    EXPECT_EQ(snapshot.device.serviceEndpoint, "https://learn.example.test");

    ASSERT_TRUE(deviceReload.Disconnect());
    EXPECT_TRUE(deviceReload.Snapshot().device.deviceToken.empty());
}

TEST(LearningSyncStore, MalformedStateFailsClosedWithoutLosingFile) {
    TemporaryStore temporary;
    {
        std::ofstream file(temporary.path);
        file << "{not json";
    }
    JPAssist::LearningSyncStore store(temporary.path.string());
    EXPECT_FALSE(store.Load());
    EXPECT_FALSE(store.LastError().empty());
    EXPECT_TRUE(store.PendingBatch(250).empty());
    EXPECT_TRUE(std::filesystem::exists(temporary.path));
}

#if !defined(_WIN32)
TEST(LearningSyncStore, RestrictsCredentialStateToCurrentUser) {
    TemporaryStore temporary;
    JPAssist::LearningSyncStore store(temporary.path.string());
    ASSERT_TRUE(store.Load());
    ASSERT_TRUE(store.SetDevice({ "device", "secret-token", "game", "adapter", "https://learn.example.test" }));

    const auto permissions = std::filesystem::status(temporary.path).permissions();
    EXPECT_EQ(permissions & std::filesystem::perms::group_all, std::filesystem::perms::none);
    EXPECT_EQ(permissions & std::filesystem::perms::others_all, std::filesystem::perms::none);
    EXPECT_NE(permissions & std::filesystem::perms::owner_read, std::filesystem::perms::none);
    EXPECT_NE(permissions & std::filesystem::perms::owner_write, std::filesystem::perms::none);
}
#endif

} // namespace
