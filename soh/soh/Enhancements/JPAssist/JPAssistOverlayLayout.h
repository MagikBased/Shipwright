#pragma once

#include <algorithm>
#include <cmath>

namespace JPAssist {

struct OverlayLayout {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float scale = 1.0f;
};

inline OverlayLayout JPAssistOverlay_ComputeLayout(float workX, float workY, float workWidth, float workHeight,
                                                    float requestedScale, bool studyMode) {
    const float scale = std::isfinite(requestedScale) ? std::clamp(requestedScale, 0.7f, 1.5f) : 1.0f;
    const float safeWidth = std::max(workWidth, 1.0f);
    const float safeHeight = std::max(workHeight, 1.0f);
    const float maxMargin = std::max((std::min(safeWidth, safeHeight) - 1.0f) * 0.5f, 0.0f);
    const float margin = std::min(24.0f * scale, maxMargin);
    const float availableWidth = std::max(safeWidth - margin * 2.0f, 1.0f);
    const float availableHeight = std::max(safeHeight - margin * 2.0f, 1.0f);
    // The dialogue panel follows the native textbox's broad horizontal
    // silhouette; Study Mode remains a compact side card.
    const float width = std::min((studyMode ? 420.0f : 960.0f) * scale, availableWidth);
    const float height = std::min((studyMode ? 460.0f : 120.0f) * scale, availableHeight);

    return {
        studyMode ? workX + safeWidth - width - margin : workX + (safeWidth - width) * 0.5f,
        studyMode ? workY + margin : workY + safeHeight - height - margin,
        width,
        height,
        scale,
    };
}

} // namespace JPAssist
