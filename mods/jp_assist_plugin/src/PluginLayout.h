#pragma once

#include <string>

#include "JPAssistOverlayLayout.h"
#include "mods/study_mod_api.h"

namespace JPAssistPlugin {

struct InteractiveLayout {
    StudyModRect panel{};
    float scale = 1.0f;
    bool showCenterGuides = false;
    bool snappedX = false;
    bool snappedY = false;
    float guideX = 0.0f;
    float guideY = 0.0f;
};

class LayoutController {
  public:
    void Initialize(const StudyModHostApi& host, const char* modId);
    InteractiveLayout Update(const StudyModOverlayFrame& frame, const StudyModRect& textboxBounds);

  private:
    enum class Interaction { None, Move, Resize };
    static const char* ProfileName(JPAssist::OverlayPlacementProfile profile);
    std::string Key(JPAssist::OverlayPlacementProfile profile, const char* field) const;
    void LoadGeometry(const StudyModOverlayFrame& frame, JPAssist::OverlayPlacementProfile profile,
                      const JPAssist::OverlayLayout& automatic);
    void PersistGeometry(const StudyModOverlayFrame& frame);
    void ClampToViewport(const StudyModOverlayFrame& frame);

    const StudyModHostApi* mHost = nullptr;
    std::string mModId;
    JPAssist::OverlayPlacementProfile mProfile = JPAssist::OverlayPlacementProfile::NoDialogue;
    StudyModRect mPanel{};
    StudyModRect mLastViewport{};
    StudyModVec2 mInteractionStartMouse{};
    StudyModRect mInteractionStartPanel{};
    Interaction mInteraction = Interaction::None;
    bool mHasGeometry = false;
    bool mStoredGeometryValid = false;
    bool mMouseWasHeld = false;
    float mScale = 1.0f;
};

} // namespace JPAssistPlugin
