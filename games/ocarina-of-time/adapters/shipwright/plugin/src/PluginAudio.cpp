#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include "PluginAudio.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include "JPAssistAudioMath.h"

namespace JPAssistPlugin {

namespace {

constexpr const char* kManifestResource = "jp_assist/audio_manifest.json";
constexpr uint32_t kOutputRate = 32000;
constexpr size_t kMaximumFrames = kOutputRate * 30;

bool SafeArchiveRelativePath(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

} // namespace

bool Audio::ReadResource(const std::string& name, std::string& output) const {
    if (mHost == nullptr || mHost->get_resource_size == nullptr || mHost->copy_resource_data == nullptr) {
        return false;
    }
    const size_t size = mHost->get_resource_size(name.c_str());
    if (size == 0) {
        return false;
    }
    output.assign(size, '\0');
    return mHost->copy_resource_data(name.c_str(), output.data(), output.size()) == size;
}

bool Audio::Initialize(const StudyModHostApi& host, const char* modId) {
    mHost = &host;
    mModId = modId == nullptr ? "" : modId;
    mResources.clear();
    mDecoded.clear();
    std::string manifestBytes;
    if (!ReadResource(kManifestResource, manifestBytes)) {
        return false;
    }
    try {
        const auto manifest = nlohmann::json::parse(manifestBytes);
        for (const auto& entry : manifest.value("entries", nlohmann::json::array())) {
            const std::string wordId = entry.value("wordId", "");
            const std::string relative = entry.value("wordAudio", "");
            if (!wordId.empty() && SafeArchiveRelativePath(relative)) {
                mResources[wordId] = (std::filesystem::path("jp_assist") / relative).generic_string();
            }
        }
        return !mResources.empty();
    } catch (...) {
        mResources.clear();
        return false;
    }
}

bool Audio::HasWord(const std::string& wordId) const {
    return mResources.contains(wordId);
}

bool Audio::Decode(const std::string& resource, std::vector<int16_t>& output) const {
    std::string bytes;
    if (!ReadResource(resource, bytes)) {
        return false;
    }
    drwav wav{};
    if (!drwav_init_memory(&wav, bytes.data(), bytes.size(), nullptr)) {
        return false;
    }
    const uint64_t frames = wav.totalPCMFrameCount;
    const uint32_t channels = wav.channels;
    const uint32_t sampleRate = wav.sampleRate;
    if (frames == 0 || channels == 0 || channels > 2 || sampleRate < 8000 || sampleRate > 192000 ||
        frames > static_cast<uint64_t>(sampleRate) * 30) {
        drwav_uninit(&wav);
        return false;
    }
    std::vector<float> source(static_cast<size_t>(frames) * channels);
    const uint64_t read = drwav_read_pcm_frames_f32(&wav, frames, source.data());
    drwav_uninit(&wav);
    if (read == 0) {
        return false;
    }
    source.resize(static_cast<size_t>(read) * channels);
    output = JPAssist::JPAssistAudio_ResampleToStereo(source.data(), read, channels, sampleRate, kOutputRate);
    return !output.empty() && output.size() / 2 <= kMaximumFrames;
}

bool Audio::PlayWord(const std::string& wordId) {
    if (mHost == nullptr || mHost->play_audio == nullptr || mModId.empty()) {
        return false;
    }
    const auto resource = mResources.find(wordId);
    if (resource == mResources.end()) {
        return false;
    }
    auto decoded = mDecoded.find(wordId);
    if (decoded == mDecoded.end()) {
        std::vector<int16_t> clip;
        if (!Decode(resource->second, clip)) {
            return false;
        }
        decoded = mDecoded.emplace(wordId, std::move(clip)).first;
    }
    const StudyModAudioClip clip{ sizeof(StudyModAudioClip), STUDY_MOD_AUDIO_PCM_S16, decoded->second.data(),
                                  decoded->second.size() / 2, kOutputRate, 2 };
    return mHost->play_audio(mModId.c_str(), &clip) != 0;
}

} // namespace JPAssistPlugin
