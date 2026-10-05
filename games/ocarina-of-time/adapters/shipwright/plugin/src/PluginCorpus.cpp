#include "PluginCorpus.h"

#include <algorithm>
#include <exception>
#include <nlohmann/json.hpp>

namespace JPAssistPlugin {

std::string Token::Id() const {
    return lemma + "|" + (dictionaryReading.empty() ? reading : dictionaryReading);
}

namespace {

uint64_t ParseDialogueId(const std::string& value) {
    return std::stoull(value, nullptr, 0);
}

} // namespace

bool Corpus::LoadFromHost(const StudyModHostApi& host, const char* resourceName) {
    if (host.get_resource_size == nullptr || host.copy_resource_data == nullptr) {
        mError = "Host does not provide resource access";
        return false;
    }
    const size_t size = host.get_resource_size(resourceName);
    if (size == 0) {
        mError = std::string("Resource not found: ") + resourceName;
        return false;
    }
    std::string contents(size, '\0');
    if (host.copy_resource_data(resourceName, contents.data(), contents.size()) != size) {
        mError = std::string("Could not copy resource: ") + resourceName;
        return false;
    }
    return LoadJson(contents);
}

bool Corpus::LoadJson(const std::string& contents) {
    mPages.clear();
    mError.clear();
    mVersion = "unavailable";
    try {
        const nlohmann::json root = nlohmann::json::parse(contents);
        if (root.contains("metadata")) {
            mVersion = root.at("metadata").value("corpusVersion", "unknown");
        }
        const nlohmann::json& messages = root.contains("messages") ? root.at("messages") : root;
        for (const auto& [key, record] : messages.items()) {
            if (!record.is_object() || !record.contains("pages")) {
                continue;
            }
            const std::string id = record.contains("source")
                                       ? record.at("source").value("messageId", key)
                                       : key;
            auto& pages = mPages[ParseDialogueId(id)];
            for (const auto& pageJson : record.at("pages")) {
                Page page;
                page.japanese = pageJson.value("japanese", "");
                page.english = pageJson.value("english", "");
                page.isChoice = pageJson.value("isChoice", false);
                for (const auto& tokenJson : pageJson.value("tokens", nlohmann::json::array())) {
                    Token token;
                    token.surface = tokenJson.value("surface", "");
                    token.lemma = tokenJson.value("lemma", token.surface);
                    token.reading = tokenJson.value("reading", "");
                    token.dictionaryReading = tokenJson.value("dictionaryReading", token.reading);
                    token.partOfSpeech = tokenJson.value("partOfSpeech", "");
                    token.meaning = tokenJson.value("meaning", "");
                    token.note = tokenJson.value("note", "");
                    token.senseId = tokenJson.value("senseId", "");
                    token.start = tokenJson.value("start", 0U);
                    token.length = tokenJson.value("length", 0U);
                    if (token.length != 0) {
                        page.tokens.push_back(std::move(token));
                    }
                }
                pages.push_back(std::move(page));
            }
        }
        if (mPages.empty()) {
            mError = "Corpus contained no usable messages";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        mPages.clear();
        mError = exception.what();
        return false;
    }
}

const Page* Corpus::FindPage(uint64_t dialogueId, uint32_t pageIndex) const {
    const auto found = mPages.find(dialogueId);
    if (found == mPages.end() || pageIndex >= found->second.size()) {
        return nullptr;
    }
    return &found->second[pageIndex];
}

bool Corpus::IsLoaded() const {
    return !mPages.empty();
}

const std::string& Corpus::Error() const {
    return mError;
}

const std::string& Corpus::Version() const { return mVersion; }

} // namespace JPAssistPlugin
