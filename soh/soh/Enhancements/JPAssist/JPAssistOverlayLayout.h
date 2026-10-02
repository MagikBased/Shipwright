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

    // Keep the familiar bottom placement unless the native textbox touches
    // the card's breathing room. Upper and middle textboxes therefore leave
    // the card where the player already expects it.
    if (layout.y >= dialogueBottom + gap || layout.y + layout.height <= dialogueTop - gap) {
        return layout;
    }

    // For a lower textbox, center the card in the unobstructed band above it.
    // Reserving the upper fifth avoids OoT's hearts and action-button HUD.
    const float hudSafeTop = workY + safeHeight * 0.22f;
    const float upperBandBottom = dialogueTop - gap;
    if (upperBandBottom - hudSafeTop >= layout.height) {
        layout.y = hudSafeTop + (upperBandBottom - hudSafeTop - layout.height) * 0.5f;
        return layout;
    }

    // Small windows may not have a full HUD-safe band. Prefer whichever side
    // of the textbox can contain the card, then clamp as a final fallback.
    const float aboveY = dialogueTop - gap - layout.height;
    if (aboveY >= workY) {
        layout.y = aboveY;
        return layout;
    }

    const float belowY = dialogueBottom + gap;
    if (belowY + layout.height <= workBottom) {
        layout.y = belowY;
        return layout;
    }

    layout.y = std::clamp(layout.y, workY, std::max(workY, workBottom - layout.height));
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
