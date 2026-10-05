#include "PluginLayout.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace JPAssistPlugin {
namespace {

bool Contains(const StudyModRect& rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x <= rect.x + rect.width && y <= rect.y + rect.height;
}

bool Changed(const StudyModRect& left, const StudyModRect& right) {
    constexpr float tolerance = 0.5f;
    return std::fabs(left.x - right.x) > tolerance || std::fabs(left.y - right.y) > tolerance ||
           std::fabs(left.width - right.width) > tolerance || std::fabs(left.height - right.height) > tolerance;
}

} // namespace

void LayoutController::Initialize(const StudyModHostApi& host, const char* modId) {
    mHost = &host;
    mModId = modId == nullptr ? "" : modId;
}

const char* LayoutController::ProfileName(JPAssist::OverlayPlacementProfile profile) {
    switch (profile) {
        case JPAssist::OverlayPlacementProfile::UpperDialogue:
            return "UpperDialogue";
        case JPAssist::OverlayPlacementProfile::LowerDialogue:
            return "LowerDialogue";
        default:
            return "NoDialogue";
    }
}

std::string LayoutController::Key(JPAssist::OverlayPlacementProfile profile, const char* field) const {
    return std::string("Layout.") + ProfileName(profile) + "." + field;
}

void LayoutController::LoadGeometry(const StudyModOverlayFrame& frame, JPAssist::OverlayPlacementProfile profile,
                                    const JPAssist::OverlayLayout& automatic) {
    mProfile = profile;
    mPanel = { automatic.x, automatic.y, automatic.width, automatic.height,
               frame.viewport.logical_screen_width, frame.viewport.logical_screen_height };
    const bool stored = mHost != nullptr && mHost->get_int_setting != nullptr &&
                        mHost->get_int_setting(mModId.c_str(), Key(profile, "Valid").c_str(), 0) != 0;
    mStoredGeometryValid = stored;
    if (stored && mHost->get_float_setting != nullptr) {
        const float widthRatio = std::clamp(mHost->get_float_setting(
                                                mModId.c_str(), Key(profile, "Width").c_str(),
                                                mPanel.width / std::max(frame.viewport.width, 1.0f)),
                                            0.0f, 1.0f);
        const float heightRatio = std::clamp(mHost->get_float_setting(
                                                 mModId.c_str(), Key(profile, "Height").c_str(),
                                                 mPanel.height / std::max(frame.viewport.height, 1.0f)),
                                             0.0f, 1.0f);
        mPanel.width = widthRatio * frame.viewport.width;
        mPanel.height = heightRatio * frame.viewport.height;
        const float xRatio = std::clamp(
            mHost->get_float_setting(mModId.c_str(), Key(profile, "X").c_str(), 0.5f), 0.0f, 1.0f);
        const float yRatio = std::clamp(
            mHost->get_float_setting(mModId.c_str(), Key(profile, "Y").c_str(), 0.5f), 0.0f, 1.0f);
        mPanel.x = frame.viewport.x + xRatio * std::max(frame.viewport.width - mPanel.width, 0.0f);
        mPanel.y = frame.viewport.y + yRatio * std::max(frame.viewport.height - mPanel.height, 0.0f);
    }
    ClampToViewport(frame);
    mLastViewport = frame.viewport;
    mHasGeometry = true;
}

void LayoutController::ClampToViewport(const StudyModOverlayFrame& frame) {
    const float minimumWidth = std::min(680.0f * mScale, frame.viewport.width);
    const float minimumHeight = std::min(120.0f * mScale, frame.viewport.height);
    mPanel.width = std::clamp(mPanel.width, minimumWidth, std::max(frame.viewport.width, minimumWidth));
    mPanel.height = std::clamp(mPanel.height, minimumHeight, std::max(frame.viewport.height, minimumHeight));
    mPanel.x = std::clamp(mPanel.x, frame.viewport.x,
                          frame.viewport.x + std::max(frame.viewport.width - mPanel.width, 0.0f));
    mPanel.y = std::clamp(mPanel.y, frame.viewport.y,
                          frame.viewport.y + std::max(frame.viewport.height - mPanel.height, 0.0f));
    mPanel.logical_screen_width = frame.viewport.logical_screen_width;
    mPanel.logical_screen_height = frame.viewport.logical_screen_height;
}

void LayoutController::PersistGeometry(const StudyModOverlayFrame& frame) {
    if (mHost == nullptr || mHost->set_int_setting == nullptr || mHost->set_float_setting == nullptr) {
        return;
    }
    const float movableWidth = std::max(frame.viewport.width - mPanel.width, 1.0f);
    const float movableHeight = std::max(frame.viewport.height - mPanel.height, 1.0f);
    mHost->set_float_setting(mModId.c_str(), Key(mProfile, "X").c_str(),
                             std::clamp((mPanel.x - frame.viewport.x) / movableWidth, 0.0f, 1.0f));
    mHost->set_float_setting(mModId.c_str(), Key(mProfile, "Y").c_str(),
                             std::clamp((mPanel.y - frame.viewport.y) / movableHeight, 0.0f, 1.0f));
    mHost->set_float_setting(mModId.c_str(), Key(mProfile, "Width").c_str(),
                             mPanel.width / std::max(frame.viewport.width, 1.0f));
    mHost->set_float_setting(mModId.c_str(), Key(mProfile, "Height").c_str(),
                             mPanel.height / std::max(frame.viewport.height, 1.0f));
    mHost->set_int_setting(mModId.c_str(), Key(mProfile, "Valid").c_str(), 1);
    mStoredGeometryValid = true;
    if (mHost->struct_size >= offsetof(StudyModHostApi, flush_settings) + sizeof(mHost->flush_settings) &&
        mHost->flush_settings != nullptr) {
        mHost->flush_settings(mModId.c_str());
    }
}

InteractiveLayout LayoutController::Update(const StudyModOverlayFrame& frame, const StudyModRect& textboxBounds) {
    const float logicalHeight = textboxBounds.logical_screen_height > 0.0f
                                    ? textboxBounds.logical_screen_height
                                    : frame.viewport.logical_screen_height;
    const bool hasTextbox = textboxBounds.width > 0.0f && textboxBounds.height > 0.0f && logicalHeight > 0.0f;
    const float textboxTop = hasTextbox ? textboxBounds.y / logicalHeight : 0.0f;
    const float textboxBottom = hasTextbox ? (textboxBounds.y + textboxBounds.height) / logicalHeight : 0.0f;
    const auto profile = JPAssist::JPAssistOverlay_ClassifyPlacement(hasTextbox, textboxTop, textboxBottom);
    const float requestedScale = mHost != nullptr && mHost->get_float_setting != nullptr
                                     ? mHost->get_float_setting(mModId.c_str(), "CardScale", 1.0f)
                                     : 1.0f;
    const auto automatic = JPAssist::JPAssistOverlay_ComputeAdaptiveLayout(
        frame.viewport.x, frame.viewport.y, frame.viewport.width, frame.viewport.height, requestedScale,
        hasTextbox, textboxTop, textboxBottom);
    mScale = automatic.scale;
    const bool storedGeometryValid = mHost != nullptr && mHost->get_int_setting != nullptr &&
                                     mHost->get_int_setting(mModId.c_str(), Key(profile, "Valid").c_str(), 0) != 0;
    if (!mHasGeometry || profile != mProfile || Changed(frame.viewport, mLastViewport) ||
        storedGeometryValid != mStoredGeometryValid) {
        LoadGeometry(frame, profile, automatic);
    }

    const bool mouseHeld = (frame.mouse_held & STUDY_MOD_MOUSE_LEFT) != 0;
    const bool mousePressed = (frame.mouse_pressed & STUDY_MOD_MOUSE_LEFT) != 0;
    const float scale = automatic.scale;
    const StudyModRect resizeGrip{ mPanel.x + mPanel.width - 24.0f * scale,
                                   mPanel.y + mPanel.height - 24.0f * scale, 24.0f * scale, 24.0f * scale,
                                   mPanel.logical_screen_width, mPanel.logical_screen_height };
    const StudyModRect moveStrip{ mPanel.x, mPanel.y, mPanel.width, std::max(12.0f * scale, 8.0f),
                                  mPanel.logical_screen_width, mPanel.logical_screen_height };
    if (mInteraction == Interaction::None && mousePressed) {
        if (Contains(resizeGrip, frame.mouse_x, frame.mouse_y)) {
            mInteraction = Interaction::Resize;
        } else if (Contains(moveStrip, frame.mouse_x, frame.mouse_y)) {
            mInteraction = Interaction::Move;
        }
        if (mInteraction != Interaction::None) {
            mInteractionStartMouse = { frame.mouse_x, frame.mouse_y };
            mInteractionStartPanel = mPanel;
        }
    }

    InteractiveLayout result;
    if (mInteraction != Interaction::None && mouseHeld) {
        const float deltaX = frame.mouse_x - mInteractionStartMouse.x;
        const float deltaY = frame.mouse_y - mInteractionStartMouse.y;
        if (mInteraction == Interaction::Move) {
            const auto drag = JPAssist::JPAssistOverlay_ApplyDragModifiers(
                mInteractionStartPanel.x, mInteractionStartPanel.y, deltaX, deltaY, mPanel.width, mPanel.height,
                frame.viewport.x, frame.viewport.y, frame.viewport.width, frame.viewport.height,
                (frame.modifiers & STUDY_MOD_MODIFIER_SHIFT) != 0,
                (frame.modifiers & STUDY_MOD_MODIFIER_CONTROL) != 0, 20.0f * scale);
            mPanel.x = drag.x;
            mPanel.y = drag.y;
            result.showCenterGuides = (frame.modifiers & STUDY_MOD_MODIFIER_CONTROL) != 0;
            result.snappedX = drag.snappedX;
            result.snappedY = drag.snappedY;
            result.guideX = drag.guideX;
            result.guideY = drag.guideY;
        } else {
            mPanel.width = mInteractionStartPanel.width + deltaX;
            mPanel.height = mInteractionStartPanel.height + deltaY;
        }
        ClampToViewport(frame);
    }
    if (mInteraction != Interaction::None && mMouseWasHeld && !mouseHeld) {
        PersistGeometry(frame);
        mInteraction = Interaction::None;
    }
    mMouseWasHeld = mouseHeld;
    result.panel = mPanel;
    result.scale = mScale;
    return result;
}

} // namespace JPAssistPlugin
