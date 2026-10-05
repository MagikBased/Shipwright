#include "PluginProgress.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>

namespace JPAssistPlugin {

bool Progress::Initialize(const StudyModHostApi& host, const char* modId) {
    if (host.get_writable_path == nullptr) {
        return false;
    }
    const size_t size = host.get_writable_path(modId, "jp_assist_progress.json", nullptr, 0);
    if (size == 0) {
        return false;
    }
    std::string path(size, '\0');
    if (host.get_writable_path(modId, "jp_assist_progress.json", path.data(), path.size()) != size) {
        return false;
    }
    path.resize(size - 1);
    mPath = std::move(path);
    return Load();
}

bool Progress::Load() {
    mSaved.clear();
    mKnown.clear();
    mEncounters.clear();
    mLastEncounter.clear();
    mDirty = false;
    if (mPath.empty() || !std::filesystem::exists(mPath)) {
        return !mPath.empty();
    }
    try {
        std::ifstream file(mPath);
        nlohmann::json root;
        file >> root;
        const auto saved = root.value("savedTokenIds", nlohmann::json::array());
        for (const auto& id : saved) {
            mSaved.insert(id.get<std::string>());
        }
        const auto known = root.value("knownTokens", nlohmann::json::array());
        for (const auto& entry : known) {
            const std::string tokenId = entry.value("tokenId", "");
            if (!tokenId.empty()) {
                mKnown.emplace(tokenId, entry.value("senseId", ""));
            }
        }
        const auto encounters = root.value("encounterCounts", nlohmann::json::object());
        for (const auto& [id, count] : encounters.items()) {
            mEncounters[id] = count.get<int>();
        }
        const auto timestamps = root.value("lastEncounterUnixTime", nlohmann::json::object());
        for (const auto& [id, timestamp] : timestamps.items()) {
            mLastEncounter[id] = timestamp.get<int64_t>();
        }
        return true;
    } catch (...) {
        mSaved.clear();
        mKnown.clear();
        mEncounters.clear();
        mLastEncounter.clear();
        return false;
    }
}

bool Progress::Save() {
    if (!mDirty || mPath.empty()) {
        return !mPath.empty();
    }
    nlohmann::json root;
    root["schemaVersion"] = 2;
    root["savedTokenIds"] = std::vector<std::string>(mSaved.begin(), mSaved.end());
    root["knownTokens"] = nlohmann::json::array();
    for (const auto& [tokenId, senseId] : mKnown) {
        root["knownTokens"].push_back({ { "tokenId", tokenId }, { "senseId", senseId } });
    }
    root["encounterCounts"] = mEncounters;
    root["lastEncounterUnixTime"] = mLastEncounter;
    const std::string temporary = mPath + ".tmp";
    try {
        {
            std::ofstream file(temporary, std::ios::trunc);
            if (!file.is_open()) {
                return false;
            }
            file << root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        std::error_code error;
        std::filesystem::rename(temporary, mPath, error);
        if (error) {
            std::filesystem::remove(mPath, error);
            error.clear();
            std::filesystem::rename(temporary, mPath, error);
        }
        if (error) {
            return false;
        }
        mDirty = false;
        return true;
    } catch (...) {
        return false;
    }
}

bool Progress::ToggleSaved(const std::string& tokenId) {
    if (mSaved.erase(tokenId) == 0) {
        mSaved.insert(tokenId);
    }
    mDirty = true;
    return IsSaved(tokenId);
}

bool Progress::MarkKnown(const std::string& tokenId, const std::string& senseId) {
    const bool inserted = mKnown.emplace(tokenId, senseId).second;
    mDirty = mDirty || inserted;
    return inserted;
}

bool Progress::IsSaved(const std::string& tokenId) const { return mSaved.contains(tokenId); }
bool Progress::IsKnown(const std::string& tokenId, const std::string& senseId) const {
    return mKnown.contains({ tokenId, senseId });
}

void Progress::RecordEncounter(const std::string& tokenId) {
    ++mEncounters[tokenId];
    mLastEncounter[tokenId] = static_cast<int64_t>(std::time(nullptr));
    mDirty = true;
}

int Progress::EncounterCount(const std::string& tokenId) const {
    const auto found = mEncounters.find(tokenId);
    return found == mEncounters.end() ? 0 : found->second;
}

} // namespace JPAssistPlugin
