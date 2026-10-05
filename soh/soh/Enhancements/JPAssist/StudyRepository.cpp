#include "StudyRepository.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "JPAssistHost.h"

namespace JPAssist {

namespace {

std::unordered_map<uint16_t, std::vector<StudyPage>> sPagesByTextId;
std::string sCorpusVersion;
std::string sLoadError;
bool sCorpusLoaded = false;

uint16_t ParseTextId(const std::string& value) {
    return static_cast<uint16_t>(std::stoul(value, nullptr, 0));
}

} // namespace

bool StudyRepository_LoadCorpus(const std::string& explicitPath) {
    sPagesByTextId.clear();
    sCorpusVersion.clear();
    sLoadError.clear();
    sCorpusLoaded = false;
    std::string sourceLabel = explicitPath.empty() ? "host data jp_assist/runtime_data.json" : explicitPath;

    try {
        nlohmann::json root;
        if (explicitPath.empty()) {
            std::string contents;
            if (!JPAssistHost_ReadDataFile("jp_assist/runtime_data.json", contents)) {
                sLoadError = "Corpus not found in host data: jp_assist/runtime_data.json";
                SPDLOG_WARN("[JPAssist] {}", sLoadError);
                return false;
            }
            root = nlohmann::json::parse(contents);
        } else {
            if (!std::filesystem::exists(explicitPath)) {
                sLoadError = "Corpus not found at " + explicitPath;
                SPDLOG_WARN("[JPAssist] {}", sLoadError);
                return false;
            }
            std::ifstream file(explicitPath);
            file >> root;
        }

        if (root.contains("metadata")) {
            sCorpusVersion = root["metadata"].value("corpusVersion", "unknown");
        } else {
            sCorpusVersion = "legacy-schema-1";
        }
        const nlohmann::json& messages = root.contains("messages") ? root["messages"] : root;

        for (const auto& [key, record] : messages.items()) {
            if (!record.is_object() || !record.contains("source") || !record.contains("pages")) {
                continue;
            }
            const std::string messageId = record["source"].value("messageId", key);
            const uint16_t textId = ParseTextId(messageId);
            auto& pages = sPagesByTextId[textId];
            for (const auto& pageJson : record["pages"]) {
                StudyPage page;
                page.japanese = pageJson.value("japanese", "");
                page.english = pageJson.value("english", "");
                page.isChoice = pageJson.value("isChoice", false);
                page.choiceCount = pageJson.value("choiceCount", 0);
                for (const auto& tokenJson : pageJson.value("tokens", nlohmann::json::array())) {
                    StudyToken token;
                    token.surface = tokenJson.value("surface", "");
                    token.lemma = tokenJson.value("lemma", token.surface);
                    token.reading = tokenJson.value("reading", "");
                    token.dictionaryReading = tokenJson.value("dictionaryReading", token.reading);
                    token.partOfSpeech = tokenJson.value("partOfSpeech", "");
                    token.meaning = tokenJson.value("meaning", "");
                    token.note = tokenJson.value("note", "");
                    token.start = tokenJson.value("start", 0U);
                    token.length = tokenJson.value("length", 0U);
                    token.senseId = tokenJson.value("senseId", "");
                    page.tokens.push_back(std::move(token));
                }
                pages.push_back(std::move(page));
            }
        }

        sCorpusLoaded = !sPagesByTextId.empty();
        if (!sCorpusLoaded) {
            sLoadError = "Corpus contained no usable messages";
            return false;
        }
        SPDLOG_INFO("[JPAssist] Loaded corpus {} with {} messages from {}", sCorpusVersion, sPagesByTextId.size(),
                    sourceLabel);
        return true;
    } catch (const std::exception& exception) {
        sLoadError = exception.what();
        sPagesByTextId.clear();
        SPDLOG_ERROR("[JPAssist] Failed to load corpus from {}: {}", sourceLabel, sLoadError);
        return false;
    }
}

bool StudyRepository_IsCorpusLoaded() {
    return sCorpusLoaded;
}

const std::string& StudyRepository_GetCorpusVersion() {
    return sCorpusVersion;
}

const std::string& StudyRepository_GetLoadError() {
    return sLoadError;
}

const StudyPage* StudyRepository_FindPage(uint16_t textId, int pageIndex) {
    const auto found = sPagesByTextId.find(textId);
    if (found == sPagesByTextId.end() || found->second.empty()) {
        return nullptr;
    }
    pageIndex = std::clamp(pageIndex, 0, static_cast<int>(found->second.size()) - 1);
    return &found->second[pageIndex];
}

size_t StudyRepository_GetPageCount(uint16_t textId) {
    const auto found = sPagesByTextId.find(textId);
    return found == sPagesByTextId.end() ? 0 : found->second.size();
}

const std::vector<StudyToken>& StudyRepository_GetTestTokens() {
    static const std::vector<StudyToken> tokens = {
        { "妖精", "妖精", "ようせい", "ようせい", "noun", "fairy", "", 0, 2 },
        { "樹", "樹", "き", "き", "noun", "tree", "As in 「デクの樹」, the Great Deku Tree.", 0, 1 },
        { "コキリ族", "コキリ族", "こきりぞく", "こきりぞく", "noun", "the Kokiri (tribe/people)", "", 0, 4 },
        { "剣", "剣", "けん", "けん", "noun", "sword", "", 0, 1 },
        { "盾", "盾", "たて", "たて", "noun", "shield", "", 0, 1 },
    };
    return tokens;
}

} // namespace JPAssist
