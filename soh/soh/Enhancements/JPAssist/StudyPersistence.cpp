#include "StudyPersistence.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <ship/Context.h>

#include "StudyRepository.h"

namespace JPAssist {

namespace {

constexpr int kSchemaVersion = 2;
constexpr size_t kMaxHistoryEntries = 20;

std::set<std::string> sSavedTokenIds;
std::set<std::pair<std::string, std::string>> sKnownTokens;
std::unordered_map<std::string, int> sEncounterCounts;
std::unordered_map<std::string, int64_t> sLastEncounterUnixTime;
std::vector<HistoryEntry> sHistory; // oldest first; trimmed to kMaxHistoryEntries
bool sLoaded = false;

std::string ProgressFilePath() {
    return Ship::Context::GetPathRelativeToAppDirectory("jp_assist_progress.json");
}

} // namespace

void StudyPersistence_Load() {
    sSavedTokenIds.clear();
    sKnownTokens.clear();
    sEncounterCounts.clear();
    sLastEncounterUnixTime.clear();
    sHistory.clear();
    sLoaded = true;

    std::string path = ProgressFilePath();
    if (!std::filesystem::exists(path)) {
        SPDLOG_INFO("[JPAssist] No existing progress file at {} - starting fresh", path);
        return;
    }

    // Mirrors soh/soh/Enhancements/Presets/Presets.cpp's LoadPresets: a
    // corrupt file just logs and leaves progress empty, rather than
    // crashing or propagating the exception - design doc 14: "Missing
    // corpus data fails gracefully" applies just as much to a broken
    // progress file as to missing corpus data.
    try {
        std::ifstream file(path);
        nlohmann::json json;
        file >> json;

        // Bind each .value(...) result to a named variable before calling
        // .items()/iterating it, rather than chaining directly in the
        // range-for. .value() returns a temporary nlohmann::json by value;
        // .items() on that temporary returns a proxy that references it,
        // but the temporary's lifetime ends at the end of the enclosing
        // full expression - *before* the loop body runs - since the
        // range-for only extends the lifetime of what it directly binds
        // to (the .items() proxy), not a sub-expression the proxy happens
        // to reference. That's a real dangling-reference bug, not
        // theoretical: it reproduced live as an inconsistent mix of a
        // bogus "type must be number, but is null" exception on one run
        // and a straight segfault on the next, tracked down with a
        // standalone repro against the exact on-disk file.
        nlohmann::json savedTokenIds = json.value("savedTokenIds", nlohmann::json::array());
        for (const auto& id : savedTokenIds) {
            sSavedTokenIds.insert(id.get<std::string>());
        }
        nlohmann::json knownTokens = json.value("knownTokens", nlohmann::json::array());
        for (const auto& known : knownTokens) {
            const std::string tokenId = known.value("tokenId", "");
            if (!tokenId.empty()) {
                sKnownTokens.emplace(tokenId, known.value("senseId", ""));
            }
        }
        nlohmann::json encounterCounts = json.value("encounterCounts", nlohmann::json::object());
        for (const auto& [id, count] : encounterCounts.items()) {
            sEncounterCounts[id] = count.get<int>();
        }
        nlohmann::json lastEncounterUnixTime = json.value("lastEncounterUnixTime", nlohmann::json::object());
        for (const auto& [id, timestamp] : lastEncounterUnixTime.items()) {
            sLastEncounterUnixTime[id] = timestamp.get<int64_t>();
        }
        nlohmann::json messageHistory = json.value("messageHistory", nlohmann::json::array());
        for (const auto& entry : messageHistory) {
            HistoryEntry historyEntry;
            historyEntry.textId = entry.value("textId", 0);
            historyEntry.englishText = entry.value("englishText", "");
            historyEntry.unixTime = entry.value("unixTime", static_cast<int64_t>(0));
            sHistory.push_back(historyEntry);
        }
        SPDLOG_INFO(
            "[JPAssist] Loaded progress: {} saved token(s), {} known sense(s), {} with encounter counts, {} history entries",
            sSavedTokenIds.size(), sKnownTokens.size(), sEncounterCounts.size(), sHistory.size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[JPAssist] Failed to parse progress file at {} ({}) - starting fresh", path, e.what());
        sSavedTokenIds.clear();
        sKnownTokens.clear();
        sEncounterCounts.clear();
        sLastEncounterUnixTime.clear();
        sHistory.clear();
    }
}

void StudyPersistence_Save() {
    nlohmann::json json;
    json["schemaVersion"] = kSchemaVersion;
    json["corpusVersion"] = StudyRepository_IsCorpusLoaded() ? StudyRepository_GetCorpusVersion() : "unavailable";
    json["savedTokenIds"] = std::vector<std::string>(sSavedTokenIds.begin(), sSavedTokenIds.end());
    json["knownTokens"] = nlohmann::json::array();
    for (const auto& [tokenId, senseId] : sKnownTokens) {
        json["knownTokens"].push_back({ { "tokenId", tokenId }, { "senseId", senseId } });
    }
    json["encounterCounts"] = sEncounterCounts;
    json["lastEncounterUnixTime"] = sLastEncounterUnixTime;
    nlohmann::json historyArray = nlohmann::json::array();
    for (const auto& entry : sHistory) {
        historyArray.push_back({ { "textId", entry.textId }, { "englishText", entry.englishText },
                                 { "unixTime", entry.unixTime } });
    }
    json["messageHistory"] = historyArray;

    std::string path = ProgressFilePath();
    std::string tmpPath = path + ".tmp";

    // Write-to-temp-then-rename, the same atomic pattern
    // soh/soh/SaveManager.cpp uses for the real .sav files - a crash or
    // power loss mid-write leaves the old file intact instead of a
    // half-written progress file that would hit the parse-failure path
    // above on next load.
    {
        std::ofstream file(tmpPath);
        if (!file.is_open()) {
            SPDLOG_ERROR("[JPAssist] Failed to open {} for writing progress", tmpPath);
            return;
        }
        // Replace any invalid UTF-8 defensively. MessageParser normalizes
        // game-font glyph bytes, but progress persistence must remain a hard
        // no-crash boundary even if another caller supplies malformed text.
        file << json.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        SPDLOG_ERROR("[JPAssist] Failed to move {} to {}: {}", tmpPath, path, ec.message());
    }
}

bool StudyPersistence_IsSaved(const std::string& tokenId) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    return sSavedTokenIds.contains(tokenId);
}

void StudyPersistence_ToggleSaved(const std::string& tokenId) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    if (sSavedTokenIds.contains(tokenId)) {
        sSavedTokenIds.erase(tokenId);
    } else {
        sSavedTokenIds.insert(tokenId);
    }
}

bool StudyPersistence_IsKnown(const std::string& tokenId, const std::string& senseId) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    return sKnownTokens.contains({ tokenId, senseId });
}

void StudyPersistence_MarkKnown(const std::string& tokenId, const std::string& senseId) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    sKnownTokens.emplace(tokenId, senseId);
}

void StudyPersistence_RecordEncounter(const std::string& tokenId) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    sEncounterCounts[tokenId]++;
    sLastEncounterUnixTime[tokenId] = static_cast<int64_t>(std::time(nullptr));
}

int StudyPersistence_GetEncounterCount(const std::string& tokenId) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    auto it = sEncounterCounts.find(tokenId);
    return it == sEncounterCounts.end() ? 0 : it->second;
}

void StudyPersistence_RecordHistoryEntry(uint16_t textId, const std::string& englishText) {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    HistoryEntry entry;
    entry.textId = textId;
    entry.englishText = englishText;
    entry.unixTime = static_cast<int64_t>(std::time(nullptr));
    sHistory.push_back(entry);
    if (sHistory.size() > kMaxHistoryEntries) {
        sHistory.erase(sHistory.begin());
    }
}

const std::vector<HistoryEntry>& StudyPersistence_GetHistory() {
    if (!sLoaded) {
        StudyPersistence_Load();
    }
    return sHistory;
}

} // namespace JPAssist
