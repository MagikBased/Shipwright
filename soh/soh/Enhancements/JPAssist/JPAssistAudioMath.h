#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace JPAssist {

// Convert arbitrary interleaved floating-point PCM to Ship's fixed 32 kHz
// stereo stream. Linear interpolation is sufficient for short spoken-word
// clips and keeps this path dependency-free after WAV decoding.
inline std::vector<int16_t> JPAssistAudio_ResampleToStereo(const float* input, uint64_t frames,
                                                           uint32_t channels, uint32_t inputRate,
                                                           uint32_t outputRate = 32000) {
    if (input == nullptr || frames == 0 || channels == 0 || inputRate == 0 || outputRate == 0) {
        return {};
    }
    const uint64_t outputFrames = std::max<uint64_t>(
        1, static_cast<uint64_t>(std::ceil(static_cast<double>(frames) * outputRate / inputRate)));
    std::vector<int16_t> output(static_cast<size_t>(outputFrames) * 2);
    for (uint64_t frame = 0; frame < outputFrames; ++frame) {
        const double sourcePosition = static_cast<double>(frame) * inputRate / outputRate;
        const uint64_t first = std::min<uint64_t>(static_cast<uint64_t>(sourcePosition), frames - 1);
        const uint64_t second = std::min<uint64_t>(first + 1, frames - 1);
        const float fraction = static_cast<float>(sourcePosition - first);
        for (uint32_t outputChannel = 0; outputChannel < 2; ++outputChannel) {
            const uint32_t sourceChannel = channels == 1 ? 0 : std::min(outputChannel, channels - 1);
            const float firstSample = input[first * channels + sourceChannel];
            const float secondSample = input[second * channels + sourceChannel];
            const float sample = std::clamp(firstSample + (secondSample - firstSample) * fraction, -1.0f, 1.0f);
            output[static_cast<size_t>(frame) * 2 + outputChannel] =
                static_cast<int16_t>(std::lround(sample * std::numeric_limits<int16_t>::max()));
        }
    }
    return output;
}

inline int16_t JPAssistAudio_MixSample(int16_t gameSample, int16_t voiceSample, float voiceGain = 0.9f) {
    const int mixed = static_cast<int>(gameSample) + static_cast<int>(std::lround(voiceSample * voiceGain));
    return static_cast<int16_t>(std::clamp(mixed, static_cast<int>(std::numeric_limits<int16_t>::min()),
                                           static_cast<int>(std::numeric_limits<int16_t>::max())));
}

} // namespace JPAssist
