#include "mods/study_mod_api.h"
#include "mods/study_mod_host_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>

#include "StudyModApiInternal.h"

namespace {

template <typename Callback>
struct Subscription {
    uint64_t id;
    std::string modId;
    Callback callback;
    void* userData;
};

std::mutex sCallbackMutex;
uint64_t sNextSubscriptionId = 1;
std::vector<Subscription<StudyModDialogueCallback>> sDialogueCallbacks;
std::vector<Subscription<StudyModInputCallback>> sInputCallbacks;
std::vector<Subscription<StudyModLifecycleCallback>> sLifecycleCallbacks;
std::vector<Subscription<StudyModNativeHighlightCallback>> sNativeHighlightCallbacks;
std::vector<Subscription<StudyModOverlayCallback>> sOverlayCallbacks;
std::mutex sAudioMutex;
std::string sAudioOwner;
std::vector<int16_t> sAudioSamples;
size_t sAudioCursor = 0;
std::mutex sTestResourceMutex;
std::unordered_map<std::string, std::string> sTestResources;
std::string sTestWritableRoot;
std::unordered_map<std::string, int32_t> sTestIntSettings;
std::unordered_map<std::string, float> sTestFloatSettings;
std::unordered_map<std::string, std::string> sTestStringSettings;

constexpr uint32_t kAudioSampleRate = 32000;
constexpr uint32_t kAudioChannels = 2;
constexpr size_t kMaximumAudioFrames = kAudioSampleRate * 30;

bool IsValidModId(const char* value) {
    if (value == nullptr || *value == '\0') {
        return false;
    }
    for (const unsigned char character : std::string(value)) {
        if (!std::isalnum(character) && character != '-' && character != '_' && character != '.') {
            return false;
        }
    }
    return std::strcmp(value, ".") != 0 && std::strcmp(value, "..") != 0;
}

bool IsSafeRelativePath(const char* value) {
    if (value == nullptr || *value == '\0') {
        return false;
    }
    const std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool IsValidSettingKey(const char* value) {
    return IsValidModId(value) && value[0] != '.';
}

std::string SettingName(const char* modId, const char* key) {
    return std::string("gEnhancements.StudyMods.") + modId + "." + key;
}

size_t CopyPath(const std::string& path, char* output, size_t outputSize) {
    const size_t required = path.size() + 1;
    if (output != nullptr && outputSize != 0) {
        const size_t copied = std::min(path.size(), outputSize - 1);
        std::memcpy(output, path.data(), copied);
        output[copied] = '\0';
    }
    return required;
}

size_t GetWritablePath(const char* modId, const char* relativePath, char* output, size_t outputSize) {
    if (!IsValidModId(modId) || !IsSafeRelativePath(relativePath)) {
        if (output != nullptr && outputSize != 0) {
            output[0] = '\0';
        }
        return 0;
    }

    std::filesystem::path privateRoot;
    {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        if (!sTestWritableRoot.empty()) {
            privateRoot = std::filesystem::path(sTestWritableRoot) / modId;
        }
    }
    if (privateRoot.empty()) {
        if (Ship::Context::GetRawInstance() == nullptr) {
            return 0;
        }
        privateRoot = Ship::Context::GetPathRelativeToAppDirectory(std::string("mods/") + modId);
    }
    const std::filesystem::path resolved = (privateRoot / relativePath).lexically_normal();
    const std::filesystem::path parent = resolved.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            if (output != nullptr && outputSize != 0) {
                output[0] = '\0';
            }
            return 0;
        }
    }
    return CopyPath(resolved.string(), output, outputSize);
}

size_t GetResourceSize(const char* resourceName) {
    if (resourceName == nullptr) {
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        const auto found = sTestResources.find(resourceName);
        if (found != sTestResources.end()) {
            return found->second.size();
        }
    }
    const auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr) {
        return 0;
    }
    const auto file = context->GetResourceManager()->LoadFileProcess(resourceName);
    return file == nullptr || file->Buffer == nullptr || file->BufferOffset > file->Buffer->size()
               ? 0
               : file->Buffer->size() - file->BufferOffset;
}

size_t CopyResourceData(const char* resourceName, void* output, size_t outputSize) {
    if (resourceName == nullptr) {
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        const auto found = sTestResources.find(resourceName);
        if (found != sTestResources.end()) {
            if (output != nullptr && outputSize != 0) {
                std::memcpy(output, found->second.data(), std::min(found->second.size(), outputSize));
            }
            return found->second.size();
        }
    }
    const auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr) {
        return 0;
    }
    const auto file = context->GetResourceManager()->LoadFileProcess(resourceName);
    if (file == nullptr || file->Buffer == nullptr || file->BufferOffset > file->Buffer->size()) {
        return 0;
    }
    const size_t size = file->Buffer->size() - file->BufferOffset;
    if (output != nullptr && outputSize != 0) {
        std::memcpy(output, file->Buffer->data() + file->BufferOffset, std::min(size, outputSize));
    }
    return size;
}

template <typename Callback>
uint64_t RegisterCallback(const char* modId, Callback callback, void* userData,
                          std::vector<Subscription<Callback>>& subscriptions) {
    if (!IsValidModId(modId) || callback == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(sCallbackMutex);
    const uint64_t id = sNextSubscriptionId++;
    subscriptions.push_back({ id, modId, callback, userData });
    return id;
}

uint64_t RegisterDialogueCallback(const char* modId, StudyModDialogueCallback callback, void* userData) {
    return RegisterCallback(modId, callback, userData, sDialogueCallbacks);
}

uint64_t RegisterInputCallback(const char* modId, StudyModInputCallback callback, void* userData) {
    return RegisterCallback(modId, callback, userData, sInputCallbacks);
}

uint64_t RegisterLifecycleCallback(const char* modId, StudyModLifecycleCallback callback, void* userData) {
    return RegisterCallback(modId, callback, userData, sLifecycleCallbacks);
}

uint64_t RegisterNativeHighlightCallback(const char* modId, StudyModNativeHighlightCallback callback,
                                         void* userData) {
    return RegisterCallback(modId, callback, userData, sNativeHighlightCallbacks);
}

uint64_t RegisterOverlayCallback(const char* modId, StudyModOverlayCallback callback, void* userData) {
    return RegisterCallback(modId, callback, userData, sOverlayCallbacks);
}

int32_t StopAudio(const char* modId) {
    if (!IsValidModId(modId)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(sAudioMutex);
    if (sAudioOwner != modId) {
        return 0;
    }
    sAudioOwner.clear();
    sAudioSamples.clear();
    sAudioCursor = 0;
    return 1;
}

int32_t PlayAudio(const char* modId, const StudyModAudioClip* clip) {
    if (!IsValidModId(modId) || clip == nullptr || clip->struct_size < sizeof(StudyModAudioClip) ||
        clip->format != STUDY_MOD_AUDIO_PCM_S16 || clip->samples == nullptr || clip->frame_count == 0 ||
        clip->frame_count > kMaximumAudioFrames || clip->sample_rate != kAudioSampleRate ||
        clip->channels != kAudioChannels) {
        return 0;
    }
    std::vector<int16_t> copied(clip->samples, clip->samples + clip->frame_count * kAudioChannels);
    std::lock_guard<std::mutex> lock(sAudioMutex);
    sAudioOwner = modId;
    sAudioSamples = std::move(copied);
    sAudioCursor = 0;
    return 1;
}

int32_t GetIntSetting(const char* modId, const char* key, int32_t defaultValue) {
    if (!IsValidModId(modId) || !IsValidSettingKey(key)) {
        return defaultValue;
    }
    {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        const auto found = sTestIntSettings.find(std::string(modId) + "." + key);
        if (found != sTestIntSettings.end()) {
            return found->second;
        }
    }
    return Ship::Context::GetRawInstance() == nullptr
               ? defaultValue
               : CVarGetInteger(SettingName(modId, key).c_str(), defaultValue);
}

int32_t SetIntSetting(const char* modId, const char* key, int32_t value) {
    if (!IsValidModId(modId) || !IsValidSettingKey(key)) {
        return 0;
    }
    if (Ship::Context::GetRawInstance() == nullptr) {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        sTestIntSettings[std::string(modId) + "." + key] = value;
        return 1;
    }
    CVarSetInteger(SettingName(modId, key).c_str(), value);
    return 1;
}

float GetFloatSetting(const char* modId, const char* key, float defaultValue) {
    if (!IsValidModId(modId) || !IsValidSettingKey(key)) {
        return defaultValue;
    }
    if (Ship::Context::GetRawInstance() == nullptr) {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        const auto found = sTestFloatSettings.find(std::string(modId) + "." + key);
        return found == sTestFloatSettings.end() ? defaultValue : found->second;
    }
    return CVarGetFloat(SettingName(modId, key).c_str(), defaultValue);
}

int32_t SetFloatSetting(const char* modId, const char* key, float value) {
    if (!IsValidModId(modId) || !IsValidSettingKey(key)) {
        return 0;
    }
    if (Ship::Context::GetRawInstance() == nullptr) {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        sTestFloatSettings[std::string(modId) + "." + key] = value;
        return 1;
    }
    CVarSetFloat(SettingName(modId, key).c_str(), value);
    return 1;
}

size_t GetStringSetting(const char* modId, const char* key, const char* defaultValue, char* output,
                        size_t outputSize) {
    const char* fallback = defaultValue == nullptr ? "" : defaultValue;
    if (!IsValidModId(modId) || !IsValidSettingKey(key)) {
        return CopyPath(fallback, output, outputSize);
    }
    if (Ship::Context::GetRawInstance() == nullptr) {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        const auto found = sTestStringSettings.find(std::string(modId) + "." + key);
        return CopyPath(found == sTestStringSettings.end() ? fallback : found->second, output, outputSize);
    }
    return CopyPath(CVarGetString(SettingName(modId, key).c_str(), fallback), output, outputSize);
}

int32_t SetStringSetting(const char* modId, const char* key, const char* value) {
    if (!IsValidModId(modId) || !IsValidSettingKey(key) || value == nullptr) {
        return 0;
    }
    if (Ship::Context::GetRawInstance() == nullptr) {
        std::lock_guard<std::mutex> lock(sTestResourceMutex);
        sTestStringSettings[std::string(modId) + "." + key] = value;
        return 1;
    }
    CVarSetString(SettingName(modId, key).c_str(), value);
    return 1;
}

int32_t FlushSettings(const char* modId) {
    if (!IsValidModId(modId)) {
        return 0;
    }
    if (Ship::Context::GetRawInstance() == nullptr) {
        return 1;
    }
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    return 1;
}

template <typename Callback>
bool EraseSubscription(uint64_t id, std::vector<Subscription<Callback>>& subscriptions) {
    const auto found = std::find_if(subscriptions.begin(), subscriptions.end(),
                                    [id](const auto& subscription) { return subscription.id == id; });
    if (found == subscriptions.end()) {
        return false;
    }
    subscriptions.erase(found);
    return true;
}

int32_t UnregisterCallback(uint64_t id) {
    if (id == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(sCallbackMutex);
    return EraseSubscription(id, sDialogueCallbacks) || EraseSubscription(id, sInputCallbacks) ||
                   EraseSubscription(id, sLifecycleCallbacks) || EraseSubscription(id, sNativeHighlightCallbacks)
                   || EraseSubscription(id, sOverlayCallbacks)
               ? 1
               : 0;
}

template <typename Callback>
uint32_t EraseModSubscriptions(const char* modId, std::vector<Subscription<Callback>>& subscriptions) {
    const size_t previousSize = subscriptions.size();
    subscriptions.erase(std::remove_if(subscriptions.begin(), subscriptions.end(),
                                       [modId](const auto& subscription) { return subscription.modId == modId; }),
                        subscriptions.end());
    return static_cast<uint32_t>(previousSize - subscriptions.size());
}

uint32_t UnregisterModCallbacks(const char* modId) {
    if (!IsValidModId(modId)) {
        return 0;
    }
    uint32_t removed = 0;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        removed = EraseModSubscriptions(modId, sDialogueCallbacks) + EraseModSubscriptions(modId, sInputCallbacks) +
                  EraseModSubscriptions(modId, sLifecycleCallbacks) +
                  EraseModSubscriptions(modId, sNativeHighlightCallbacks) +
                  EraseModSubscriptions(modId, sOverlayCallbacks);
    }
    StopAudio(modId);
    return removed;
}

const StudyModHostApi kHostApiV1 = {
    STUDY_MOD_HOST_ABI_VERSION_1,
    sizeof(StudyModHostApi),
        STUDY_MOD_CAP_WRITABLE_STORAGE | STUDY_MOD_CAP_RESOURCE_DATA | STUDY_MOD_CAP_DIALOGUE_EVENTS |
        STUDY_MOD_CAP_SEMANTIC_INPUT | STUDY_MOD_CAP_NATIVE_TEXT_HIGHLIGHT | STUDY_MOD_CAP_AUDIO_PLAYBACK |
        STUDY_MOD_CAP_LIFECYCLE_EVENTS | STUDY_MOD_CAP_SETTINGS | STUDY_MOD_CAP_OVERLAY,
    "ship-of-harkinian",
    "ocarina-of-time",
    GetWritablePath,
    GetResourceSize,
    CopyResourceData,
    RegisterDialogueCallback,
    RegisterInputCallback,
    RegisterLifecycleCallback,
    UnregisterCallback,
    UnregisterModCallbacks,
    RegisterNativeHighlightCallback,
    PlayAudio,
    StopAudio,
    GetIntSetting,
    SetIntSetting,
    GetFloatSetting,
    SetFloatSetting,
    GetStringSetting,
    SetStringSetting,
    RegisterOverlayCallback,
    FlushSettings,
};

} // namespace

extern "C" const StudyModHostApi* StudyMod_GetHostApi(uint32_t minimumVersion, uint32_t maximumVersion) {
    if (minimumVersion > STUDY_MOD_HOST_ABI_VERSION_1 || maximumVersion < STUDY_MOD_HOST_ABI_VERSION_1 ||
        minimumVersion > maximumVersion) {
        return nullptr;
    }
    return &kHostApiV1;
}

namespace StudyModApi {

bool HasRegisteredMod(const char* modId) {
    if (!IsValidModId(modId)) {
        return false;
    }
    const auto matches = [modId](const auto& subscription) { return subscription.modId == modId; };
    std::lock_guard<std::mutex> lock(sCallbackMutex);
    return std::any_of(sDialogueCallbacks.begin(), sDialogueCallbacks.end(), matches) ||
           std::any_of(sInputCallbacks.begin(), sInputCallbacks.end(), matches) ||
           std::any_of(sLifecycleCallbacks.begin(), sLifecycleCallbacks.end(), matches) ||
           std::any_of(sNativeHighlightCallbacks.begin(), sNativeHighlightCallbacks.end(), matches) ||
           std::any_of(sOverlayCallbacks.begin(), sOverlayCallbacks.end(), matches);
}

void DispatchDialogue(const StudyModDialogueEvent& event) {
    std::vector<Subscription<StudyModDialogueCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        callbacks = sDialogueCallbacks;
    }
    for (const auto& subscription : callbacks) {
        subscription.callback(&event, subscription.userData);
    }
}

uint64_t DispatchInput(const StudyModInputEvent& event) {
    std::vector<Subscription<StudyModInputCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        callbacks = sInputCallbacks;
    }
    uint64_t consumed = 0;
    for (const auto& subscription : callbacks) {
        consumed |= subscription.callback(&event, subscription.userData);
    }
    return (consumed & event.pressed) | (consumed & STUDY_MOD_INPUT_CONSUME_AXIS_MASK);
}

void DispatchLifecycle(const StudyModLifecycleEvent& event) {
    std::vector<Subscription<StudyModLifecycleCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        callbacks = sLifecycleCallbacks;
    }
    for (const auto& subscription : callbacks) {
        subscription.callback(&event, subscription.userData);
    }
}

void DispatchOverlay(const StudyModOverlayFrame& frame, const StudyModOverlayDrawApi& draw) {
    std::vector<Subscription<StudyModOverlayCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        callbacks = sOverlayCallbacks;
    }
    for (const auto& subscription : callbacks) {
        subscription.callback(&frame, &draw, subscription.userData);
    }
}

bool QueryNativeHighlight(uint64_t dialogueId, StudyModNativeHighlight& highlight) {
    std::vector<Subscription<StudyModNativeHighlightCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        callbacks = sNativeHighlightCallbacks;
    }
    // The most recently registered provider gets first refusal, matching the
    // normal patch/mod override convention used by asset archives.
    for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it) {
        StudyModNativeHighlight candidate{ sizeof(StudyModNativeHighlight), 0, 0, STUDY_MOD_HIGHLIGHT_GLOW, 0 };
        if (it->callback(dialogueId, &candidate, it->userData) != 0 && candidate.length != 0) {
            highlight = candidate;
            return true;
        }
    }
    return false;
}

void MixAudio(int16_t* samples, size_t frameCount) {
    if (samples == nullptr || frameCount == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(sAudioMutex);
    const size_t requestedSamples = frameCount * kAudioChannels;
    const size_t remaining = sAudioSamples.size() - std::min(sAudioCursor, sAudioSamples.size());
    const size_t mixedSamples = std::min(requestedSamples, remaining);
    for (size_t i = 0; i < mixedSamples; ++i) {
        const int32_t mixed = static_cast<int32_t>(samples[i]) +
                              static_cast<int32_t>(static_cast<float>(sAudioSamples[sAudioCursor + i]) * 0.9f);
        samples[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
    }
    sAudioCursor += mixedSamples;
    if (sAudioCursor >= sAudioSamples.size()) {
        sAudioOwner.clear();
        sAudioSamples.clear();
        sAudioCursor = 0;
    }
}

bool IsAudioPlaying() {
    std::lock_guard<std::mutex> lock(sAudioMutex);
    return !sAudioSamples.empty();
}

void ClearCallbacks() {
    std::lock_guard<std::mutex> lock(sCallbackMutex);
    sDialogueCallbacks.clear();
    sInputCallbacks.clear();
    sLifecycleCallbacks.clear();
    sNativeHighlightCallbacks.clear();
    sOverlayCallbacks.clear();
    {
        std::lock_guard<std::mutex> audioLock(sAudioMutex);
        sAudioOwner.clear();
        sAudioSamples.clear();
        sAudioCursor = 0;
    }
}

void SetResourceForTesting(const std::string& name, std::string data) {
    std::lock_guard<std::mutex> lock(sTestResourceMutex);
    sTestResources[name] = std::move(data);
}

void ClearResourcesForTesting() {
    std::lock_guard<std::mutex> lock(sTestResourceMutex);
    sTestResources.clear();
    sTestWritableRoot.clear();
    sTestIntSettings.clear();
    sTestFloatSettings.clear();
    sTestStringSettings.clear();
}

void SetWritableRootForTesting(const std::string& path) {
    std::lock_guard<std::mutex> lock(sTestResourceMutex);
    sTestWritableRoot = path;
}

void SetIntSettingForTesting(const std::string& modId, const std::string& key, int32_t value) {
    std::lock_guard<std::mutex> lock(sTestResourceMutex);
    sTestIntSettings[modId + "." + key] = value;
}

} // namespace StudyModApi

extern "C" int32_t StudyModHost_QueryNativeHighlight(uint64_t dialogueId, uint32_t* start, uint32_t* length) {
    if (start == nullptr || length == nullptr) {
        return 0;
    }
    StudyModNativeHighlight highlight{};
    if (!StudyModApi::QueryNativeHighlight(dialogueId, highlight)) {
        return 0;
    }
    *start = highlight.start;
    *length = highlight.length;
    return 1;
}
