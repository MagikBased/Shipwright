#include "JPAssistAudio.h"
#include "JPAssistAudioMath.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <dr_wav.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "JPAssistHost.h"
#include "mods/study_mod_api.h"

namespace JPAssist {
namespace {

constexpr uint32_t kOutputRate = 32000;
constexpr size_t kMaximumDecodedFrames = kOutputRate * 30;

std::mutex sAudioMutex;
std::unordered_map<std::string, std::filesystem::path> sWordAudioPaths;
std::unordered_map<std::string, std::shared_ptr<const std::vector<int16_t>>> sDecodedClips;
std::string sLoadError;

std::string ResolveManifestPath(const std::string& explicitPath) {
    if (!explicitPath.empty()) {
        return explicitPath;
    }
    return JPAssistHost_LocateDataFile("jp_assist/audio_manifest.json");
}

bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

std::shared_ptr<const std::vector<int16_t>> DecodeClip(const std::filesystem::path& path) {
    drwav wav = {};
    if (!drwav_init_file(&wav, path.string().c_str(), nullptr)) {
        SPDLOG_WARN("[JPAssist] Could not decode word audio {}", path.string());
        return nullptr;
    }
    const uint64_t frames = wav.totalPCMFrameCount;
    if (frames == 0 || wav.channels == 0 || wav.channels > 2 || wav.sampleRate < 8000 || wav.sampleRate > 192000 ||
        frames > static_cast<uint64_t>(wav.sampleRate) * 30) {
        drwav_uninit(&wav);
        SPDLOG_WARN("[JPAssist] Rejected unsupported, empty, or overlong word audio {}", path.string());
        return nullptr;
    }
    std::vector<float> source(static_cast<size_t>(frames) * wav.channels);
    const uint64_t read = drwav_read_pcm_frames_f32(&wav, frames, source.data());
    const uint32_t channels = wav.channels;
    const uint32_t sampleRate = wav.sampleRate;
    drwav_uninit(&wav);
    if (read == 0) {
        return nullptr;
    }
    source.resize(static_cast<size_t>(read) * channels);
    auto decoded = JPAssistAudio_ResampleToStereo(source.data(), read, channels, sampleRate, kOutputRate);
    if (decoded.size() / 2 > kMaximumDecodedFrames) {
        return nullptr;
    }
    return std::make_shared<const std::vector<int16_t>>(std::move(decoded));
}

} // namespace

bool JPAssistAudio_LoadManifest(const std::string& explicitPath) {
    std::lock_guard<std::mutex> lock(sAudioMutex);
    sWordAudioPaths.clear();
    sDecodedClips.clear();
    sLoadError.clear();

    const std::filesystem::path manifestPath = ResolveManifestPath(explicitPath);
    if (manifestPath.empty() || !std::filesystem::exists(manifestPath)) {
        sLoadError = "Optional audio manifest not installed";
        SPDLOG_INFO("[JPAssist] {}", sLoadError);
        return false;
    }
    try {
        std::ifstream source(manifestPath);
        nlohmann::json root;
        source >> root;
        const auto manifestRoot = std::filesystem::weakly_canonical(manifestPath.parent_path());
        for (const auto& entry : root.value("entries", nlohmann::json::array())) {
            const std::string wordId = entry.value("wordId", "");
            const std::string relativeAudio = entry.value("wordAudio", "");
            if (wordId.empty() || relativeAudio.empty()) {
                continue;
            }
            const auto audioPath = std::filesystem::weakly_canonical(manifestRoot / relativeAudio);
            if (!IsWithin(manifestRoot, audioPath) || !std::filesystem::is_regular_file(audioPath)) {
                SPDLOG_WARN("[JPAssist] Ignoring unsafe or missing audio for {}", wordId);
                continue;
            }
            sWordAudioPaths.emplace(wordId, audioPath);
        }
        SPDLOG_INFO("[JPAssist] Loaded {} optional word-audio entries from {}", sWordAudioPaths.size(),
                    manifestPath.string());
        return !sWordAudioPaths.empty();
    } catch (const std::exception& exception) {
        sLoadError = exception.what();
        sWordAudioPaths.clear();
        SPDLOG_WARN("[JPAssist] Failed to load optional audio manifest: {}", sLoadError);
        return false;
    }
}

bool JPAssistAudio_HasWord(const std::string& wordId) {
    std::lock_guard<std::mutex> lock(sAudioMutex);
    return sWordAudioPaths.contains(wordId);
}

bool JPAssistAudio_PlayWord(const std::string& wordId) {
    std::filesystem::path audioPath;
    std::shared_ptr<const std::vector<int16_t>> clip;
    {
        std::lock_guard<std::mutex> lock(sAudioMutex);
        const auto path = sWordAudioPaths.find(wordId);
        if (path == sWordAudioPaths.end()) {
            return false;
        }
        const auto cached = sDecodedClips.find(wordId);
        if (cached != sDecodedClips.end()) {
            clip = cached->second;
        } else {
            audioPath = path->second;
        }
    }

    if (clip == nullptr) {
        // Do file I/O and resampling without holding the cache mutex.
        auto decoded = DecodeClip(audioPath);
        if (decoded == nullptr || decoded->empty()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(sAudioMutex);
        clip = sDecodedClips.emplace(wordId, std::move(decoded)).first->second;
    }

    const StudyModHostApi* api = StudyMod_GetHostApi(STUDY_MOD_HOST_ABI_VERSION_1, STUDY_MOD_HOST_ABI_VERSION_1);
    if (api == nullptr || (api->capabilities & STUDY_MOD_CAP_AUDIO_PLAYBACK) == 0 || api->play_audio == nullptr) {
        return false;
    }
    const StudyModAudioClip request{ sizeof(StudyModAudioClip), STUDY_MOD_AUDIO_PCM_S16, clip->data(),
                                     clip->size() / 2, kOutputRate, 2 };
    return api->play_audio("jp-assist", &request) != 0;
}

} // namespace JPAssist
