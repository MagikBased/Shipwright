#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace JPAssist {

// Loads the optional local audio index at jp_assist/audio_manifest.json.
// Missing or invalid audio never prevents Study Mode from loading.
bool JPAssistAudio_LoadManifest(const std::string& explicitPath = "");
bool JPAssistAudio_HasWord(const std::string& wordId);
bool JPAssistAudio_PlayWord(const std::string& wordId);

} // namespace JPAssist
