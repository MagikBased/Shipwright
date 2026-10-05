#include "mods/study_mod_api.h"
#include "PluginRuntime.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#define JPASSIST_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define JPASSIST_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

enum PluginStatus : int32_t {
    NotInitialized = 0,
    Ready = 1,
    IncompatibleHost = -1,
    MissingCapabilities = -2,
    RegistrationFailed = -3,
    CorpusUnavailable = -4,
    WrongGame = -5,
};

constexpr const char* kModId = "jp-assist";
const StudyModHostApi* sHostApi = nullptr;
int32_t sPluginStatus = NotInitialized;
uint32_t sDialogueEventCount = 0;
uint32_t sInputEventCount = 0;
uint32_t sLifecycleEventCount = 0;
uint32_t sHighlightQueryCount = 0;
uint32_t sOverlayFrameCount = 0;
uint64_t sConsumeMask = 0;
JPAssistPlugin::Runtime sRuntime;

const char* StatusMessage(int32_t status) {
    switch (status) {
        case Ready:
            return "Ready";
        case IncompatibleHost:
            return "The host does not provide a compatible Study Mod API.";
        case MissingCapabilities:
            return "The host is missing one or more required Study Mod capabilities.";
        case RegistrationFailed:
            return "The plugin could not register its host callbacks.";
        case CorpusUnavailable:
            return "The packaged JP Assist corpus is missing or invalid.";
        case WrongGame:
            return "This JP Assist package supports Ocarina of Time only.";
        default:
            return "Not initialized";
    }
}

void SetStatus(int32_t status) {
    sPluginStatus = status;
    std::fprintf(stderr, "[JP Assist plugin] %s\n", StatusMessage(status));
}

const StudyModHostApi* ResolveHostApi() {
    using GetHostApi = const StudyModHostApi* (*)(uint32_t, uint32_t);
#if defined(_WIN32)
    const auto query = reinterpret_cast<GetHostApi>(
        GetProcAddress(GetModuleHandleW(nullptr), "StudyMod_GetHostApi"));
#else
    const auto query = reinterpret_cast<GetHostApi>(dlsym(RTLD_DEFAULT, "StudyMod_GetHostApi"));
#endif
    return query == nullptr ? nullptr
                            : query(STUDY_MOD_HOST_ABI_VERSION_1, STUDY_MOD_HOST_ABI_VERSION_1);
}

void OnDialogue(const StudyModDialogueEvent* event, void*) {
    ++sDialogueEventCount;
    if (event != nullptr) {
        sRuntime.OnDialogue(*event);
    }
}

uint64_t OnInput(const StudyModInputEvent* event, void*) {
    ++sInputEventCount;
    return event == nullptr ? 0 : (sRuntime.OnInput(*event) | sConsumeMask);
}

void OnLifecycle(const StudyModLifecycleEvent* event, void*) {
    ++sLifecycleEventCount;
    if (event != nullptr) {
        sRuntime.OnLifecycle(*event);
    }
}

int32_t OnNativeHighlight(uint64_t dialogueId, StudyModNativeHighlight* highlight, void*) {
    ++sHighlightQueryCount;
    return highlight == nullptr ? 0 : sRuntime.OnNativeHighlight(dialogueId, *highlight);
}

void OnOverlay(const StudyModOverlayFrame* frame, const StudyModOverlayDrawApi* draw, void*) {
    ++sOverlayFrameCount;
    if (frame != nullptr && draw != nullptr) {
        sRuntime.OnOverlay(*frame, *draw);
    }
}

} // namespace

JPASSIST_PLUGIN_EXPORT void ModInit() {
    constexpr uint64_t required = STUDY_MOD_CAP_WRITABLE_STORAGE | STUDY_MOD_CAP_RESOURCE_DATA |
                                  STUDY_MOD_CAP_DIALOGUE_EVENTS | STUDY_MOD_CAP_SEMANTIC_INPUT |
                                  STUDY_MOD_CAP_OVERLAY | STUDY_MOD_CAP_NATIVE_TEXT_HIGHLIGHT |
                                  STUDY_MOD_CAP_AUDIO_PLAYBACK | STUDY_MOD_CAP_LIFECYCLE_EVENTS |
                                  STUDY_MOD_CAP_SETTINGS;
    sHostApi = ResolveHostApi();
    constexpr size_t requiredHostSize =
        offsetof(StudyModHostApi, register_overlay_callback) +
        sizeof(((StudyModHostApi*)nullptr)->register_overlay_callback);
    if (sHostApi == nullptr || sHostApi->abi_version != STUDY_MOD_HOST_ABI_VERSION_1 ||
        sHostApi->struct_size < requiredHostSize) {
        SetStatus(IncompatibleHost);
        return;
    }
    if (sHostApi->game_id == nullptr || std::strcmp(sHostApi->game_id, "ocarina-of-time") != 0) {
        SetStatus(WrongGame);
        return;
    }
    if ((sHostApi->capabilities & required) != required) {
        SetStatus(MissingCapabilities);
        return;
    }
    if (!sRuntime.Initialize(*sHostApi)) {
        sRuntime.Shutdown();
        SetStatus(CorpusUnavailable);
        return;
    }
    if (sHostApi->register_dialogue_callback(kModId, OnDialogue, nullptr) == 0 ||
        sHostApi->register_input_callback(kModId, OnInput, nullptr) == 0 ||
        sHostApi->register_lifecycle_callback(kModId, OnLifecycle, nullptr) == 0 ||
        sHostApi->register_native_highlight_callback(kModId, OnNativeHighlight, nullptr) == 0 ||
        sHostApi->register_overlay_callback(kModId, OnOverlay, nullptr) == 0) {
        sHostApi->unregister_mod_callbacks(kModId);
        sRuntime.Shutdown();
        SetStatus(RegistrationFailed);
        return;
    }
    SetStatus(Ready);
}

JPASSIST_PLUGIN_EXPORT void ModExit() {
    sRuntime.SetEnabled(false);
    if (sHostApi != nullptr) {
        sHostApi->unregister_mod_callbacks(kModId);
    }
    sRuntime.Shutdown();
    sHostApi = nullptr;
    sPluginStatus = NotInitialized;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_GetStatus() {
    return sPluginStatus;
}

JPASSIST_PLUGIN_EXPORT const char* JPAssistPlugin_GetStatusMessage() {
    return StatusMessage(sPluginStatus);
}

JPASSIST_PLUGIN_EXPORT uint32_t JPAssistPlugin_GetDialogueEventCount() {
    return sDialogueEventCount;
}

JPASSIST_PLUGIN_EXPORT uint32_t JPAssistPlugin_GetInputEventCount() {
    return sInputEventCount;
}

JPASSIST_PLUGIN_EXPORT uint32_t JPAssistPlugin_GetLifecycleEventCount() {
    return sLifecycleEventCount;
}

JPASSIST_PLUGIN_EXPORT void JPAssistPlugin_SetConsumeMask(uint64_t mask) {
    sConsumeMask = mask;
}

JPASSIST_PLUGIN_EXPORT void JPAssistPlugin_SetRuntimeEnabled(int32_t enabled) {
    sRuntime.SetEnabled(enabled != 0);
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_IsCorpusLoaded() {
    return sRuntime.IsCorpusLoaded() ? 1 : 0;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_GetSelectedTokenIndex() {
    return sRuntime.SelectedTokenIndex();
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_IsDefinitionVisible() {
    return sRuntime.DefinitionVisible() ? 1 : 0;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_IsSelectedTokenSaved() {
    return sRuntime.SelectedTokenSaved() ? 1 : 0;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_IsSelectedTokenKnown() {
    return sRuntime.SelectedTokenKnown() ? 1 : 0;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_SelectedTokenHasAudio() {
    return sRuntime.SelectedTokenHasAudio() ? 1 : 0;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_GetSelectedTokenEncounterCount() {
    return sRuntime.SelectedTokenEncounterCount();
}

JPASSIST_PLUGIN_EXPORT size_t JPAssistPlugin_GetPendingSyncEventCount() {
    return sRuntime.PendingSyncEventCount();
}

JPASSIST_PLUGIN_EXPORT uint32_t JPAssistPlugin_GetHighlightQueryCount() {
    return sHighlightQueryCount;
}

JPASSIST_PLUGIN_EXPORT uint32_t JPAssistPlugin_GetOverlayFrameCount() {
    return sOverlayFrameCount;
}

JPASSIST_PLUGIN_EXPORT int32_t JPAssistPlugin_PlayTestAudio() {
    static constexpr int16_t samples[] = { 1000, -1000, 2000, -2000 };
    const StudyModAudioClip clip{ sizeof(StudyModAudioClip), STUDY_MOD_AUDIO_PCM_S16, samples, 2, 32000, 2 };
    return sHostApi == nullptr ? 0 : sHostApi->play_audio(kModId, &clip);
}
