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
                                                    float requestedScale) {
    const float scale = std::isfinite(requestedScale) ? std::clamp(requestedScale, 0.7f, 1.5f) : 1.0f;
    const float safeWidth = std::max(workWidth, 1.0f);
    const float safeHeight = std::max(workHeight, 1.0f);
    const float maxMargin = std::max((std::min(safeWidth, safeHeight) - 1.0f) * 0.5f, 0.0f);
    const float margin = std::min(24.0f * scale, maxMargin);
    const float availableWidth = std::max(safeWidth - margin * 2.0f, 1.0f);
    const float availableHeight = std::max(safeHeight - margin * 2.0f, 1.0f);
    // Study Mode follows the native textbox's broad horizontal silhouette.
    // Its two-column content keeps the English reference and word card
    // readable without covering a large portion of the playfield.
    const float width = std::min(1040.0f * scale, availableWidth);
    const float height = std::min(230.0f * scale, availableHeight);

    return {
        workX + (safeWidth - width) * 0.5f,
        workY + safeHeight - height - margin,
        width,
        height,
        scale,
    };
}

} // namespace JPAssist
