#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "mods/study_mod_api.h"

namespace StudyModApi {

bool HasRegisteredMod(const char* modId);
void DispatchDialogue(const StudyModDialogueEvent& event);
uint64_t DispatchInput(const StudyModInputEvent& event);
void DispatchLifecycle(const StudyModLifecycleEvent& event);
void DispatchOverlay(const StudyModOverlayFrame& frame, const StudyModOverlayDrawApi& draw);
void QueueNativeInputForTesting(uint16_t buttons, int8_t stickY = 0, bool hasStickY = false);
bool QueryNativeHighlight(uint64_t dialogueId, StudyModNativeHighlight& highlight);
void MixAudio(int16_t* samples, size_t frameCount);
bool IsAudioPlaying();
void ClearCallbacks();
void SetResourceForTesting(const std::string& name, std::string data);
void ClearResourcesForTesting();
void SetWritableRootForTesting(const std::string& path);
void SetIntSettingForTesting(const std::string& modId, const std::string& key, int32_t value);

} // namespace StudyModApi
