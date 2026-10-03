#pragma once

#include <algorithm>
#include <cmath>

namespace JPAssist {

enum class OverlayPlacementProfile {
    NoDialogue,
    UpperDialogue,
    LowerDialogue,
};

inline OverlayPlacementProfile JPAssistOverlay_ClassifyPlacement(bool hasDialogueBounds,
                                                                  float dialogueTopNormalized,
                                                                  float dialogueBottomNormalized) {
    if (!hasDialogueBounds || !std::isfinite(dialogueTopNormalized) ||
        !std::isfinite(dialogueBottomNormalized) || dialogueBottomNormalized <= dialogueTopNormalized) {
        return OverlayPlacementProfile::NoDialogue;
    }
    return (dialogueTopNormalized + dialogueBottomNormalized) * 0.5f < 0.5f
               ? OverlayPlacementProfile::UpperDialogue
               : OverlayPlacementProfile::LowerDialogue;
}

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
    // Its three-column content keeps the English reference and word card
    // readable without covering a large portion of the playfield.
    const float width = std::min(1040.0f * scale, availableWidth);
    const float height = std::min(130.0f * scale, availableHeight);

    return {
        workX + (safeWidth - width) * 0.5f,
        workY + safeHeight - height - margin,
        width,
        height,
        scale,
    };
}

inline OverlayLayout JPAssistOverlay_PlaceAroundDialogue(OverlayLayout layout, float workY, float workHeight,
                                                          bool hasDialogueBounds, float dialogueTopNormalized,
                                                          float dialogueBottomNormalized) {
    if (!hasDialogueBounds || !std::isfinite(dialogueTopNormalized) ||
        !std::isfinite(dialogueBottomNormalized) || dialogueBottomNormalized <= dialogueTopNormalized) {
        return layout;
    }

    const float safeHeight = std::max(workHeight, 1.0f);
    const float workBottom = workY + safeHeight;
    const float dialogueTop = workY + std::clamp(dialogueTopNormalized, 0.0f, 1.0f) * safeHeight;
    const float dialogueBottom = workY + std::clamp(dialogueBottomNormalized, 0.0f, 1.0f) * safeHeight;
    const float gap = 12.0f * layout.scale;
    const float edgeMargin = std::max(workBottom - (layout.y + layout.height), 0.0f);
    const float topEdgeY = workY + edgeMargin;
    const float bottomEdgeY = workBottom - edgeMargin - layout.height;
    const float aboveY = dialogueTop - gap - layout.height;
    const float belowY = dialogueBottom + gap;
    const bool aboveFits = aboveY >= topEdgeY;
    const bool belowFits = belowY + layout.height <= workBottom - edgeMargin;
    const float dialogueCenter = (dialogueTop + dialogueBottom) * 0.5f;
    const float workCenter = workY + safeHeight * 0.5f;

    // Keep the study card visually attached to the dialogue it explains.
    // Lower textboxes prefer the immediately adjacent space above; upper
    // textboxes prefer the immediately adjacent space below.
    if (dialogueCenter >= workCenter) {
        if (aboveFits) {
            layout.y = aboveY;
            return layout;
        }
        if (belowFits) {
            layout.y = belowY;
            return layout;
        }
    } else {
        if (belowFits) {
            layout.y = belowY;
            return layout;
        }
        if (aboveFits) {
            layout.y = aboveY;
            return layout;
        }
    }

    // A very short viewport may fit neither adjacent position. Snap to the
    // screen edge opposite the dialogue instead of floating in the middle.
    layout.y = dialogueCenter >= workCenter ? topEdgeY : bottomEdgeY;
    return layout;
}

inline OverlayLayout JPAssistOverlay_ComputeAdaptiveLayout(float workX, float workY, float workWidth,
                                                            float workHeight, float requestedScale,
                                                            bool hasDialogueBounds, float dialogueTopNormalized,
                                                            float dialogueBottomNormalized) {
    return JPAssistOverlay_PlaceAroundDialogue(
        JPAssistOverlay_ComputeLayout(workX, workY, workWidth, workHeight, requestedScale), workY, workHeight,
        hasDialogueBounds, dialogueTopNormalized, dialogueBottomNormalized);
}

} // namespace JPAssist
