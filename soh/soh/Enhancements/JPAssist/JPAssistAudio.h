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

// Called by Ship's audio producer thread immediately before submitting a
// 32 kHz stereo S16 batch to the selected backend.
void JPAssistAudio_Mix(int16_t* samples, size_t frameCount);

} // namespace JPAssist
